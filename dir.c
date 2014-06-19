/*
 * Copyright (c) 2011 Tim van der Molen <tbvdm@xs4all.nl>
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

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "siren.h"

struct dir {
	char			*dir;
	struct dir_entry	 entry;
	DIR			*dirp;
	struct dirent		*dp;
};

void
dir_close(struct dir *d)
{
	while (closedir(d->dirp) == -1 && errno == EINTR);
	free(d->dir);
	free(d->entry.path);
	free(d->dp);
	free(d);
}

struct dir_entry *
dir_get_entry(struct dir *d)
{
	struct dirent	*result;
	struct stat	 sb;
	int		 ret;

	if ((ret = readdir_r(d->dirp, d->dp, &result)) || result == NULL) {
		if (ret) {
			errno = ret;
			LOG_ERR("readdir_r: %s", d->dir);
		}
		return NULL;
	}

	d->entry.name = d->dp->d_name;
	(void)xsnprintf(d->entry.path, d->entry.pathsize, "%s/%s", d->dir,
	    d->entry.name);

	if (stat(d->entry.path, &sb) == -1) {
		LOG_ERR("stat: %s", d->entry.path);
		return NULL;
	}

	switch (sb.st_mode & S_IFMT) {
	case S_IFDIR:
		d->entry.type = FILE_TYPE_DIRECTORY;
		break;
	case S_IFREG:
		d->entry.type = FILE_TYPE_REGULAR;
		break;
	default:
		d->entry.type = FILE_TYPE_OTHER;
		break;
	}

	return &d->entry;
}

struct dir *
dir_open(const char *dir)
{
	struct dir	*d;
	DIR		*dirp;
	size_t		 direntsize, pathsize;
#ifndef NAME_MAX
	long int	 namemax;
	int		 fd, oerrno;
#endif

	if ((dirp = opendir(dir)) == NULL) {
		if (errno != EACCES && errno != ENOENT && errno != ENOTDIR)
			LOG_ERR("opendir: %s", dir);
		return NULL;
	}

	/*
	 * In order to allocate space for struct dirent, we need to know the
	 * maximum length of the file names in the specified directory. If
	 * NAME_MAX is defined, we use that. Otherwise, we obtain the value
	 * from fpathconf().
	 *
	 * We use dirfd() and fpathconf() instead of pathconf() to avoid a race
	 * condition. See
	 * <http://womble.decadentplace.org.uk/readdir_r-advisory.html>.
	 */
#ifdef NAME_MAX
	direntsize = offsetof(struct dirent, d_name) + NAME_MAX + 1;
	pathsize = strlen(dir) + NAME_MAX + 2;
#else
	if ((fd = dirfd(dirp)) == -1) {
		LOG_ERR("dirfd: %s", dir);
		goto error;
	}

	if ((namemax = xfpathconf(fd, _PC_NAME_MAX)) == -1) {
		if (errno == 0)
			/*
			 * The value for _PC_NAME_MAX is indeterminate. While
			 * this a valid result, it is one we cannot do much
			 * with. Therefore, set errno to a somewhat appropriate
			 * value and return failure.
			 */
			errno = ENOTSUP;
		goto error;
	}

	direntsize = offsetof(struct dirent, d_name) + namemax + 1;
	pathsize = strlen(dir) + namemax + 2;
#endif

	if (sizeof(struct dirent) > direntsize)
		direntsize = sizeof(struct dirent);

	d = xmalloc(sizeof *d);
	d->dirp = dirp;
	d->dir = xstrdup(dir);
	d->entry.pathsize = pathsize;
	d->entry.path = xmalloc(d->entry.pathsize);
	d->dp = xmalloc(direntsize);

	return d;

#ifndef NAME_MAX
error:
	oerrno = errno;
	while (closedir(dirp) == -1 && errno == EINTR);
	errno = oerrno;
	return NULL;
#endif
}
