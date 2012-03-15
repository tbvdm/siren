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

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "siren.h"

int
xasprintf(char **buf, const char *fmt, ...)
{
	va_list	ap;
	int	ret;

	va_start(ap, fmt);
	ret = xvasprintf(buf, fmt, ap);
	va_end(ap);

	return ret;
}

void *
xcalloc(size_t nmemb, size_t size)
{
	void *p;

	if ((p = calloc(nmemb, size)) == NULL)
		LOG_FATAL("calloc");
	return p;
}

void *
xmalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		LOG_FATAL("malloc");
	return p;
}

void *
xrealloc(void *p, size_t size)
{
	void *newp;

	if ((newp = realloc(p, size)) == NULL)
		LOG_FATAL("realloc");
	return newp;
}

void *
xrecalloc(void *p, size_t nmemb, size_t size)
{
	if (nmemb == 0 || size == 0)
		LOG_FATALX("requested memory size is 0");
	if (nmemb > SIZE_MAX / size)
		LOG_FATALX("requested memory size too large");

	return xrealloc(p, nmemb * size);
}

int
xsnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list	ap;
	int	ret;

	va_start(ap, fmt);
	ret = xvsnprintf(buf, size, fmt, ap);
	va_end(ap);

	return ret;
}

char *
xstrdup(const char *s)
{
	char *c;

	if ((c = strdup(s)) == NULL)
		LOG_FATAL("strdup");
	return c;
}

char *
xstrndup(const char *s, size_t maxlen)
{
	char *t;

	if ((t = strndup(s, maxlen)) == NULL)
		LOG_FATAL("strndup");
	return t;
}

int
xvasprintf(char **buf, const char *fmt, va_list ap)
{
	int ret;

	if ((ret = vasprintf(buf, fmt, ap)) == -1)
		LOG_FATALX("vasprintf() failed");
	return ret;
}

int
xvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	int ret;

	if ((ret = vsnprintf(buf, size, fmt, ap)) == -1)
		LOG_FATALX("vsnprintf() failed");
	return ret;
}
