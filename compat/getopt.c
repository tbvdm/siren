/*
 * Copyright (c) 2011 Tim van der Molen <tim@kariliq.nl>
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

#include "../config.h"

#include <string.h>

#include "../compat.h"

#ifdef HAVE_ERR
#include <err.h>
#endif

int	 xopterr = 1;
int	 xoptind = 1;
int	 xoptopt = '\0';
int	 xoptreset;
char	*xoptarg = NULL;

/*
 * An implementation of the POSIX getopt() function with the BSD optreset
 * extension.
 */
int
xgetopt(int argc, char * const *argv, const char *optstr)
{
	static int	 i = 0;
	int		 ret;
	const char	*c;

	if (xoptind >= argc)
		return -1;

	if (xoptreset == 1)
		i = xoptreset = 0;

	if (i == 0) {
		/*
		 * We have finished if the argument is NULL, does not start
		 * with a dash or is the string "-".
		 */
		if (argv[xoptind] == NULL || argv[xoptind][0] != '-' ||
		    argv[xoptind][1] == '\0')
			return -1;
		/* We have also finished if the argument is the string "--". */
		if (argv[xoptind][1] == '-' && argv[xoptind][2] == '\0') {
			xoptind++;
			return -1;
		}
		i = 1;
	}

	xoptopt = argv[xoptind][i];
	if ((c = strchr(optstr, xoptopt)) != NULL)
		ret = xoptopt;
	else {
		ret = '?';
		if (xopterr && *optstr != ':')
			warnx("-%c: invalid option", xoptopt);
	}

	if (ret == '?' || c[1] != ':') {
		if (argv[xoptind][i + 1] != '\0')
			i++;
		else {
			xoptind++;
			i = 0;
		}
	} else {
		if (argv[xoptind][i + 1] != '\0')
			xoptarg = argv[xoptind] + i + 1;
		else {
			xoptarg = argv[++xoptind];
			if (xoptind >= argc) {
				ret = *optstr == ':' ? ':' : '?';
				if (xopterr && *optstr != ':')
					warnx("-%c: missing option argument",
					    xoptopt);
			}
		}
		xoptind++;
		i = 0;
	}

	return ret;
}
