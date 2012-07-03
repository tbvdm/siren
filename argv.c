/*
 * Copyright (c) 2012 Tim van der Molen <tbvdm@xs4all.nl>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <glob.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "siren.h"

#define ARGV_ERROR_LENGTH	1
#define ARGV_ERROR_BACKSLASH	2
#define ARGV_ERROR_QUOTE	3

#define ARGV_QUOTED_NONE	0
#define ARGV_QUOTED_SINGLE	1
#define ARGV_QUOTED_DOUBLE	2

static void argv_unescape(char **);

static void
argv_add_args(char ***argv, int *argc, char **args, size_t nargs)
{
	size_t i;

	*argv = xrecalloc(*argv, (size_t)*argc + nargs, sizeof **argv);

	for (i = 0; i < nargs; i++)
		(*argv)[(*argc)++] = xstrdup(args[i]);
}

const char *
argv_error(int errnum)
{
	switch (errnum) {
	case ARGV_ERROR_LENGTH:
		return "Argument too long";
	case ARGV_ERROR_BACKSLASH:
		return "Syntax error: backslash at end of line";
	case ARGV_ERROR_QUOTE:
		return "Syntax error: quotation mark missing";
	default:
		return "Unknown error";
	}
}

static void
argv_expand_tilde(char **arg)
{
	size_t	 userlen;
	char	*home, *narg, *user;

	if ((userlen = strcspn(*arg + 1, "/")) == 0)
		home = path_get_home_dir(NULL);
	else {
		user = xstrndup(*arg + 1, userlen);
		home = path_get_home_dir(user);
		free(user);
	}

	if (home != NULL) {
		(void)xasprintf(&narg, "%s%s", home, *arg + userlen + 1);
		free(home);
		free(*arg);
		*arg = narg;
	}
}

void
argv_free(int argc, char **argv)
{
	while (argc-- > 0)
		free(argv[argc]);
	free(argv);
}

static char *
argv_get_arg(const char **line, int *error)
{
	size_t	 len;
	int	 done, escaped, have_arg, quoted;
	char	*arg;

	len = 0;
	done = *error = escaped = have_arg = 0;
	quoted = ARGV_QUOTED_NONE;

	if (strlen(*line) > (SIZE_MAX - 1) / 2) {
		*error = ARGV_ERROR_LENGTH;
		return NULL;
	}

	/*
	 * Allocating twice the length of *line is more than sufficient. Add 1
	 * to avoid a potential zero-length allocation.
	 */
	arg = xmalloc(strlen(*line) * 2 + 1);

	while (!done)
		if (quoted == ARGV_QUOTED_NONE)
			switch (**line) {
			case '\0':
				if (escaped)
					*error = ARGV_ERROR_BACKSLASH;
				done = 1;
				break;
			case '#':
				if (!escaped)
					done = 1;
				else {
					arg[len++] = *(*line)++;
					have_arg = 1;
					escaped = 0;
				}
				break;
			case '\t':
			case ' ':
				if (!escaped) {
					if (have_arg)
						done = 1;
				} else {
					arg[len++] = **line;
					have_arg = 1;
					escaped = 0;
				}
				(*line)++;
				break;
			case '\\':
				if (!escaped)
					escaped = 1;
				else {
					arg[len++] = '\\';
					arg[len++] = '\\';
					escaped = 0;
				}
				have_arg = 1;
				(*line)++;
				break;
			case '\'':
				if (!escaped)
					quoted = ARGV_QUOTED_SINGLE;
				else {
					arg[len++] = **line;
					escaped = 0;
				}
				have_arg = 1;
				(*line)++;
				break;
			case '"':
				if (!escaped)
					quoted = ARGV_QUOTED_DOUBLE;
				else {
					arg[len++] = **line;
					escaped = 0;
				}
				have_arg = 1;
				(*line)++;
				break;
			case '*':
			case '?':
			case '[':
				/* Prevent expansion by glob(). */
				if (escaped) {
					arg[len++] = '\\';
					escaped = 0;
				}
				arg[len++] = *(*line)++;
				have_arg = 1;
				break;
			case '~':
				/*
				 * An escaped tilde at the start of an argument
				 * needs to remain escaped to prevent
				 * expansion by argv_expand_tilde().
				 *
				 * A tilde also needs escaping if it follows
				 * an empty quotation at the beginning of the
				 * argument (e.g. ""~).
				 */
				if ((escaped || have_arg) && len == 0)
					arg[len++] = '\\';
				arg[len++] = *(*line)++;
				have_arg = 1;
				escaped = 0;
				break;
			default:
				arg[len++] = *(*line)++;
				have_arg = 1;
				escaped = 0;
				break;
			}
		else
			switch (**line) {
			case '\0':
				*error = ARGV_ERROR_QUOTE;
				done = 1;
				break;
			case '\\':
				if (!escaped)
					escaped = 1;
				else {
					arg[len++] = '\\';
					arg[len++] = '\\';
					escaped = 0;
				}
				(*line)++;
				break;
			case '\'':
				if (quoted == ARGV_QUOTED_SINGLE) {
					if (!escaped)
						quoted = ARGV_QUOTED_NONE;
					else
						arg[len++] = **line;
				} else {
					if (escaped)
						arg[len++] = '\\';
					arg[len++] = **line;
				}
				escaped = 0;
				(*line)++;
				break;
			case '"':
				if (quoted == ARGV_QUOTED_DOUBLE) {
					if (!escaped)
						quoted = ARGV_QUOTED_NONE;
					else
						arg[len++] = **line;
				} else {
					if (escaped)
						arg[len++] = '\\';
					arg[len++] = **line;
				}
				escaped = 0;
				(*line)++;
				break;
			case '*':
			case '?':
			case '[':
				/* Prevent expansion by glob(). */
				if (escaped) {
					arg[len++] = '\\';
					arg[len++] = '\\';
					escaped = 0;
				}
				arg[len++] = '\\';
				arg[len++] = *(*line)++;
				break;
			case '~':
				/* Prevent expansion by argv_expand_tilde(). */
				if (escaped) {
					arg[len++] = '\\';
					arg[len++] = '\\';
					escaped = 0;
				}
				if (len == 0)
					arg[len++] = '\\';
				arg[len++] = *(*line)++;
				break;
			default:
				if (escaped) {
					arg[len++] = '\\';
					arg[len++] = '\\';
					escaped = 0;
				}
				arg[len++] = *(*line)++;
				break;
			}

	if (have_arg && *error == 0)
		arg[len] = '\0';
	else {
		free(arg);
		arg = NULL;
	}

	return arg;
}

int
argv_parse(const char *line, int *argc, char ***argv)
{
	glob_t	 gl;
	int	 error;
	char	*arg;

	*argc = 0;
	*argv = NULL;

	while ((arg = argv_get_arg(&line, &error)) != NULL) {
		if (arg[0] == '~')
			argv_expand_tilde(&arg);

		if (glob(arg, 0, NULL, &gl)) {
			argv_unescape(&arg);
			argv_add_args(argv, argc, &arg, 1);
		} else {
			argv_add_args(argv, argc, gl.gl_pathv, gl.gl_pathc);
			globfree(&gl);
		}

		free(arg);
	}

	if (error)
		argv_free(*argc, *argv);

	return error;
}

static void
argv_unescape(char **arg)
{
	size_t i, len;

	len = strlen(*arg);
	for (i = 0; i < len; i++)
		if ((*arg)[i] == '\\') {
			(void)memmove(*arg + i, *arg + i + 1, len - i);
			len--;
		}
}
