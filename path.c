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

#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "siren.h"

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
	if ((pathsize = xpathconf("/", _PC_PATH_MAX)) == -1)
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
path_get_dirname(const char *path)
{
	size_t i;

	if (path == NULL || path[0] == '\0')
		return xstrdup(".");

	i = strlen(path) - 1;

	while (path[i] == '/' && i > 0)
		i--;

	while (path[i] != '/' && i > 0)
		i--;

	if (path[i] != '/')
		return xstrdup(".");

	while (path[i] == '/' && i > 0)
		i--;

	return xstrndup(path, i + 1);
}

char *
path_get_home_dir(const char *user)
{
	struct passwd	 pw, *pwp;
	long int	 bufsize;
	int		 ret;
	char		*buf, *home;

	if (user == NULL) {
		home = getenv("HOME");
		if (home != NULL && home[0] != '\0')
			return xstrdup(home);
	}

	if ((bufsize = xsysconf(_SC_GETPW_R_SIZE_MAX)) == -1)
		return NULL;

	buf = xmalloc(bufsize);

	if (user == NULL)
		ret = getpwuid_r(getuid(), &pw, buf, bufsize, &pwp);
	else
		ret = getpwnam_r(user, &pw, buf, bufsize, &pwp);

	if (ret == 0 && pwp != NULL)
		home = xstrdup(pw.pw_dir);
	else
		home = NULL;

	free(buf);
	return home;
}

/*
 * Normalise a path:
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
	char	*cwd, *npath, *tmp;
	size_t	 i, j, pathlen;

	tmp = NULL;
	if (path[0] != '/') {
		cwd = path_get_cwd();
		xasprintf(&tmp, "%s/%s", cwd, path);
		free(cwd);
		path = tmp;
	}

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
			 * Remove the last path element including its preceding
			 * slash.
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
