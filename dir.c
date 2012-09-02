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
		/* Error or end of directory stream. */
		if ((errno = ret))
			LOG_ERR("readdir_r: %s", d->dir);
		return NULL;
	}

	d->entry.name = d->dp->d_name;
	(void)xsnprintf(d->entry.path, d->entry.pathsize, "%s/%s", d->dir,
	    d->entry.name);

	if (stat(d->entry.path, &sb) == -1) {
		LOG_ERR("stat: %s", d->entry.path);
		d->entry.type = FILE_TYPE_OTHER;
	} else {
		errno = 0;
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
	}

	return &d->entry;
}

#ifndef NAME_MAX
/*
 * Return the maximum number of bytes in a file name (excluding "\0") for the
 * specified directory. If an error occurs, 0 is returned.
 *
 * This function is used only if the system has not defined NAME_MAX in
 * <limits.h>. Otherwise the value is obtained from fpathconf().
 *
 * We use dirfd() and fpathconf() instead of pathconf() to avoid a race
 * condition. See <http://womble.decadentplace.org.uk/readdir_r-advisory.html>
 * for more information.
 */
NONNULL() static size_t
dir_get_name_max(DIR *dirp)
{
	long int	name_max;
	int		fd;

	if ((fd = dirfd(dirp)) == -1) {
		LOG_ERR("dirfd");
		return 0;
	}

	if ((name_max = XFPATHCONF(fd, _PC_NAME_MAX)) == -1) {
		if (errno == 0)
			errno = ENOTSUP;
		return 0;
	}

	return (size_t)name_max;
}
#endif

struct dir *
dir_open(const char *dir)
{
	struct dir	*d;
	DIR		*dirp;
	size_t		 dirent_size, name_max;
#ifndef NAME_MAX
	int		 oerrno;
#endif

	if ((dirp = opendir(dir)) == NULL) {
		if (errno != EACCES && errno != ENOENT && errno != ENOTDIR)
			LOG_ERR("opendir: %s", dir);
		return NULL;
	}

#ifdef NAME_MAX
	name_max = NAME_MAX;
#else
	if ((name_max = dir_get_name_max(dirp)) == 0) {
		oerrno = errno;
		while (closedir(dirp) == -1 && errno == EINTR);
		errno = oerrno;
		return NULL;
	}
#endif

	dirent_size = offsetof(struct dirent, d_name) + name_max + 1;
	if (sizeof(struct dirent) > dirent_size)
		dirent_size = sizeof(struct dirent);

	d = xmalloc(sizeof *d);
	d->dirp = dirp;
	d->dir = xstrdup(dir);
	d->entry.pathsize = strlen(dir) + name_max + 2;
	d->entry.path = xmalloc(d->entry.pathsize);
	d->dp = xmalloc(dirent_size);

	return d;
}
