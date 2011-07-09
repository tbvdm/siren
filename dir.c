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
	struct dir		*subdir;
};

void
dir_close(struct dir *d)
{
	if (d->subdir != NULL)
		dir_close(d->subdir);

	while (closedir(d->dirp) == -1 && errno == EINTR);
	free(d->dir);
	free(d->entry.path);
	free(d->dp);
	free(d);
}

int
dir_get_entry(struct dir *d, struct dir_entry **e)
{
	struct dirent	*result;
	struct stat	 sb;
	int		 ret;

	*e = NULL;

	if ((ret = readdir_r(d->dirp, d->dp, &result))) {
		errno = ret;
		LOG_ERR("readdir_r: %s", d->dir);
		return errno;
	}

	if (result == NULL)
		return 0;

	d->entry.name = d->dp->d_name;
	(void)xsnprintf(d->entry.path, d->entry.pathsize, "%s/%s", d->dir,
	    d->entry.name);

	if (stat(d->entry.path, &sb) == -1) {
		LOG_ERR("stat: %s", d->entry.path);
		return errno;
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

	*e = &d->entry;
	return 0;
}

#ifndef NAME_MAX
/*
 * Return the maximum number of bytes in a file name (excluding "\0") for the
 * specified directory. If an error occurs, 0 is returned.
 *
 * This function is used only if the system has not defined NAME_MAX in
 * <limits.h>. Otherwise the value is obtained by calling fpathconf() or
 * pathconf() for the specified directory.
 *
 * There exists a race condition between the call to opendir() and the call to
 * pathconf(). This race can be averted by using dirfd() and fpathconf().
 * Unfortunately, dirfd() is not available on all UNIX systems (e.g. AIX,
 * HP-UX and Solaris). We use fpathconf() on systems where dirfd() is
 * available. On other systems we resort to pathconf() and hope for the best.
 *
 * See <http://womble.decadentplace.org.uk/readdir_r-advisory.html> for more
 * information.
 */
/* ARGSUSED1 */
NONNULL() static size_t
dir_get_name_max(const char *dir, UNUSED DIR *dirp)
{
	long int	name_max;
#ifdef HAVE_DIRFD
	int		fd;

	if ((fd = dirfd(dirp)) == -1) {
		LOG_ERR("dirfd: %s", dir);
		return 0;
	}

	errno = 0;
	if ((name_max = fpathconf(fd, _PC_NAME_MAX)) == -1) {
		if (errno)
			LOG_ERR("fpathconf: %s", dir);
		else {
			LOG_ERRX("%s: fpathconf() failed", dir);
			errno = ENOTSUP;
		}
		return 0;
	}
#else
	errno = 0;
	if ((name_max = pathconf(dir, _PC_NAME_MAX)) == -1) {
		if (errno)
			LOG_ERR("pathconf: %s", dir);
		else {
			LOG_ERRX("%s: pathconf() failed", dir);
			errno = ENOTSUP;
		}
		return 0;
	}
#endif
	return (size_t)name_max;
}
#endif

int
dir_get_track(struct dir *d, struct track **t)
{
	struct dir_entry	*de;
	int			 ret;

	*t = NULL;

	for (;;) {
		if (d->subdir == NULL) {
			if ((ret = dir_get_entry(d, &de)) || de == NULL)
				return ret;

			if (!strcmp(de->name, ".") || !strcmp(de->name, ".."))
				continue;

			if (de->type == FILE_TYPE_DIRECTORY) {
				if ((d->subdir = dir_open(de->path)) == NULL)
					continue;
			} else if (de->type == FILE_TYPE_REGULAR) {
				if ((*t = track_init(de->path, NULL)) != NULL)
					return 0;
				continue;
			} else
				continue;
		}

		if ((ret = dir_get_track(d->subdir, t)) || *t != NULL)
			return ret;

		dir_close(d->subdir);
		d->subdir = NULL;
	}
}

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
		LOG_ERR("opendir: %s", dir);
		return NULL;
	}

#ifdef NAME_MAX
	name_max = NAME_MAX;
#else
	if ((name_max = dir_get_name_max(dirp, dir)) == 0) {
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
	d->subdir = NULL;

	return d;
}
