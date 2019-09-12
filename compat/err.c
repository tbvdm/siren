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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../compat.h"

void
err(int ret, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(ret, fmt, ap);
}

void
errx(int ret, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verrx(ret, fmt, ap);
}

void
verr(int ret, const char *fmt, va_list ap)
{
	vwarn(fmt, ap);
	exit(ret);
}

void
verrx(int ret, const char *fmt, va_list ap)
{
	vwarnx(fmt, ap);
	exit(ret);
}

void
vwarn(const char *fmt, va_list ap)
{
	int	oerrno;
	char	errstr[STRERROR_BUFSIZE];

	oerrno = errno;

	fputs(getprogname(), stderr);
	if (fmt != NULL) {
		fputs(": ", stderr);
		vfprintf(stderr, fmt, ap);
	}
	strerror_r(oerrno, errstr, sizeof errstr);
	fprintf(stderr, ": %s\n", errstr);

	errno = oerrno;
}

void
vwarnx(const char *fmt, va_list ap)
{
	fputs(getprogname(), stderr);
	if (fmt != NULL) {
		fputs(": ", stderr);
		vfprintf(stderr, fmt, ap);
	}
	putc('\n', stderr);
}

void
warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarn(fmt, ap);
	va_end(ap);
}

void
warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
}
