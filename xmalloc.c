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

/* Let glibc expose vasprintf(). */
#define _GNU_SOURCE

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
xmalloc(size_t size)
{
	void *p;

	if (size == 0)
		LOG_FATALX("zero-size allocation");
	if ((p = malloc(size)) == NULL)
		LOG_FATAL("malloc");
	return p;
}

void *
xrealloc(void *p, size_t size)
{
	void *newp;

	if (size == 0)
		LOG_FATALX("zero-size allocation");
	if ((newp = realloc(p, size)) == NULL)
		LOG_FATAL("realloc");
	return newp;
}

void *
xreallocarray(void *p, size_t nmemb, size_t size)
{
	void *newp;

	if (nmemb == 0 || size == 0)
		LOG_FATALX("zero-size allocation");
	if ((newp = reallocarray(p, nmemb, size)) == NULL)
		LOG_FATAL("reallocarray");
	return newp;
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
	char *t;

	if ((t = strdup(s)) == NULL)
		LOG_FATAL("strdup");
	return t;
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

	if ((ret = vasprintf(buf, fmt, ap)) < 0)
		LOG_FATALX("vasprintf() failed");
	return ret;
}

int
xvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	int ret;

	if ((ret = vsnprintf(buf, size, fmt, ap)) < 0)
		LOG_FATALX("vsnprintf() failed");
	return ret;
}
