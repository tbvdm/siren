/*
 * Copyright (c) 2011, 2012 Tim van der Molen <tbvdm@xs4all.nl>
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

#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "siren.h"

/*
 * Absolutise a relative path by prepending the current working directory to
 * it.
 */
static char *
path_absolutise(const char *path)
{
	char *abspath, *cwd;

	if (path[0] == '/')
		return xstrdup(path);

	cwd = path_get_cwd();
	(void)xasprintf(&abspath, "%s/%s", cwd, path);
	free(cwd);

	return abspath;
}

/*
 * Replace a leading tilde, optionally followed by a user name, in a path with
 * the user's home directory.
 */
static char *
path_expand_tilde(const char *path)
{
	size_t	 userlen;
	char	*home, *user, *newpath;

	if (path[0] != '~')
		return NULL;

	/* Skip tilde. */
	path++;

	if ((userlen = strcspn(path, "/")) == 0)
		home = path_get_home_dir(NULL);
	else {
		user = xstrndup(path, userlen);
		home = path_get_home_dir(user);
		free(user);
	}

	if (home == NULL)
		return NULL;

	(void)xasprintf(&newpath, "%s%s", home, path + userlen);
	free(home);

	return newpath;
}

/*
 * Return the current working directory. If the current working directory
 * cannot be determined, the root directory is returned.
 */
char *
path_get_cwd(void)
{
	size_t		 pathlen;
	long int	 pathsize;
	char		*path;

#ifdef PATH_MAX
	pathsize = PATH_MAX;
#else
	if ((pathsize = XPATHCONF("/", _PC_PATH_MAX)) == -1)
		return xstrdup("/");

	/*
	 * The size returned by pathconf() is relative to the root directory,
	 * so add 1 for the root directory itself. Also add 1 for the null
	 * character since some systems do not include space for it.
	 */
	pathsize += 2;
#endif

	path = xmalloc(pathsize);
	if (getcwd(path, pathsize) == NULL) {
		LOG_ERR("getcwd");
		free(path);
		return xstrdup("/");
	}

	pathlen = strlen(path);
	if ((size_t)pathsize > pathlen + 1)
		path = xrealloc(path, pathlen + 1);

	return path;
}

char *
path_get_home_dir(const char *user)
{
	struct passwd	 pw, *pwp;
	long int	 bufsize;
	int		 ret;
	char		*buf, *home;

	if (user == NULL && (home = getenv("HOME")) != NULL && home[0] != '\0')
		return xstrdup(home);

	if ((bufsize = XSYSCONF(_SC_GETPW_R_SIZE_MAX)) == -1)
		return NULL;

	buf = xmalloc(bufsize);

	if (user == NULL) {
		if ((ret = getpwuid_r(getuid(), &pw, buf, bufsize, &pwp))) {
			errno = ret;
			LOG_ERR("getpwuid_r");
		}
	} else
		if ((ret = getpwnam_r(user, &pw, buf, bufsize, &pwp))) {
			errno = ret;
			LOG_ERR("getpwnam_r");
		}

	home = (!ret && pwp != NULL) ? xstrdup(pw.pw_dir) : NULL;
	free(buf);

	return home;
}

/*
 * Normalise a path:
 * - Expand a leading tilde to a user's home directory.
 * - Absolutise a relative path by prepending the current working directory to
 *   it.
 * - Remove references to the current directory (".").
 * - Resolve references to the parent directory ("..").
 * - Remove superfluous slashes.
 *
 * Symbolic links are intentionally not resolved.
 */
char *
path_normalise(const char *path)
{
	char	*npath, *tmp;
	size_t	 i, j, pathlen;

	tmp = NULL;
	if (path[0] == '~' && (tmp = path_expand_tilde(path)) != NULL)
		path = tmp;
	if (path[0] != '/')
		path = tmp = path_absolutise(path);

	pathlen = strlen(path);
	npath = xmalloc(pathlen + 1);

	i = j = 0;
	while (i < pathlen) {
		while (path[i] == '/')
			i++;

		/* Check for reference to current directory. */
		if (path[i] == '.' && (path[i + 1] == '/' ||
		    path[i + 1] == '\0'))
			i += 2;
		/* Check for reference to parent directory. */
		else if (path[i] == '.' && path[i + 1] == '.' &&
		    (path[i + 2] == '/' || path[i + 2] == '\0')) {
			/*
			 * Remove the last path element including its
			 * preceeding slash.
			 */
			while (j > 0 && npath[--j] != '/')
				;
			i += 3;
		} else if (path[i] != '\0') {
			npath[j++] = '/';
			while (path[i] != '/' && path[i] != '\0')
				npath[j++] = path[i++];
		}
	}
	free(tmp);

	if (j == 0)
		npath[j++] = '/';
	npath[j] = '\0';

	if (j != pathlen)
		npath = xrealloc(npath, j + 1);

	return npath;
}
