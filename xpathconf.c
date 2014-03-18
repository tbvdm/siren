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

#include <errno.h>
#include <unistd.h>

#include "siren.h"

long int
xfpathconf(int fd, int name)
{
	long int value;

	errno = 0;
	if ((value = fpathconf(fd, name)) == -1) {
		if (errno)
			LOG_ERR("fpathconf: variable %d", name);
		else
			LOG_ERRX("fpathconf: variable %d: indeterminate value",
			    name);
	}

	return value;
}

long int
xpathconf(const char *path, int name)
{
	long int value;

	errno = 0;
	if ((value = pathconf(path, name)) == -1) {
		if (errno)
			LOG_ERR("pathconf: %s: variable %d", path, name);
		else
			LOG_ERRX("pathconf: %s: variable %d: indeterminate "
			    "value", path, name);
	}

	return value;
}

long int
xsysconf(int name)
{
	long int value;

	errno = 0;
	if ((value = sysconf(name)) == -1) {
		if (errno)
			LOG_ERR("sysconf: variable %d", name);
		else
			LOG_ERRX("sysconf: variable %d: indeterminate value",
			    name);
	}

	return value;
}
