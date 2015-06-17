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
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "siren.h"

#define CACHE_BUFSIZE	4096
#define CACHE_VERSION	1

static int		 cache_open_read(const char *);
static int		 cache_open_write(const char *);
static int		 cache_read_number(unsigned int *);
static int		 cache_read_string(char **);
static void		 cache_write_number(unsigned int);
static void		 cache_write_string(const char *);

static unsigned int	 cache_version;
static FILE		*cache_fp;
static size_t		 cache_bufidx;
static size_t		 cache_buflen;
static size_t		 cache_bufsize;
static char		*cache_buf;

void
cache_close(void)
{
	fclose(cache_fp);
	free(cache_buf);
}

int
cache_open(enum cache_mode mode)
{
	int	 ret;
	char	*path;

	path = conf_get_path(CACHE_FILE);

	if (mode == CACHE_MODE_READ)
		ret = cache_open_read(path);
	else
		ret = cache_open_write(path);

	free(path);
	return ret;
}

static int
cache_open_read(const char *path)
{
	cache_fp = fopen(path, "r");
	if (cache_fp == NULL) {
		if (errno != ENOENT) {
			LOG_ERR("fopen: %s", path);
			msg_err("Cannot open metadata cache file");
		}
		return -1;
	}

	cache_bufidx = 0;
	cache_buflen = 0;
	cache_bufsize = CACHE_BUFSIZE;
	cache_buf = xmalloc(cache_bufsize);

	if (cache_read_number(&cache_version) == -1) {
		msg_errx("Cannot read metadata cache file");
		goto error;
	}

	LOG_INFO("reading version %u", cache_version);

	if (cache_version > CACHE_VERSION) {
		LOG_ERRX("unsupported metadata cache version");
		msg_errx("Unsupported metadata cache version");
		goto error;
	}

	return 0;

error:
	free(cache_buf);
	fclose(cache_fp);
	return -1;
}

static int
cache_open_write(const char *path)
{
	cache_fp = fopen(path, "w");
	if (cache_fp == NULL) {
		LOG_ERR("fopen: %s", path);
		msg_err("Cannot open metadata cache file");
		return -1;
	}

	LOG_INFO("writing version %u", CACHE_VERSION);

	cache_buf = NULL;
	cache_write_number(CACHE_VERSION);
	return 0;
}

int
cache_read_entry(struct track *t)
{
	int ret;

	t->ip = NULL;
	t->ipdata = NULL;

	ret = 0;
	ret |= cache_read_string(&t->path);
	ret |= cache_read_string(&t->artist);
	ret |= cache_read_string(&t->album);
	ret |= cache_read_string(&t->date);
	if (cache_version == 0)
		t->discnumber = NULL;
	else
		ret |= cache_read_string(&t->discnumber);
	ret |= cache_read_string(&t->tracknumber);
	ret |= cache_read_string(&t->title);
	ret |= cache_read_number(&t->duration);
	ret |= cache_read_string(&t->genre);
	return ret;
}

static int
cache_read_field(char **field)
{
	size_t	 fieldlen;
	char	*sep;

	for (;;) {
		/* Fill the buffer. */
		if (cache_buflen < cache_bufsize) {
			cache_buflen += fread(cache_buf + cache_buflen, 1,
			    cache_bufsize - cache_buflen, cache_fp);
			if (ferror(cache_fp)) {
				LOG_ERR("fread");
				return -1;
			}
			if (feof(cache_fp) && cache_buflen == 0)
				return -1;
		}

		/* Find the field separator. */
		sep = memchr(cache_buf + cache_bufidx, '\0',
		    cache_buflen - cache_bufidx);
		if (sep != NULL)
			break;

		if (feof(cache_fp)) {
			LOG_ERRX("no field separator at EOF");
			return -1;
		}

		if (cache_bufidx > 0) {
			memmove(cache_buf, cache_buf + cache_bufidx,
			    cache_buflen - cache_bufidx);
			cache_buflen -= cache_bufidx;
			cache_bufidx = 0;
		} else {
			if (SIZE_MAX - cache_bufsize < CACHE_BUFSIZE) {
				LOG_ERRX("buffer size too large");
				return -1;
			}
			cache_bufsize += CACHE_BUFSIZE;
			cache_buf = xrealloc(cache_buf, cache_bufsize);
		}
	}

	*field = cache_buf + cache_bufidx;
	fieldlen = (sep - (cache_buf + cache_bufidx)) + 1;

	if (cache_bufidx + fieldlen < cache_buflen)
		cache_bufidx += fieldlen;
	else {
		cache_bufidx = 0;
		cache_buflen = 0;
	}

	return 0;
}

static int
cache_read_number(unsigned int *num)
{
	char		*field;
	const char	*errstr;

	if (cache_read_field(&field) == -1)
		return -1;

	*num = strtonum(field, 0, UINT_MAX, &errstr);
	if (errstr != NULL) {
		LOG_ERRX("%s: number is %s", field, errstr);
		return -1;
	}

	return 0;
}

static int
cache_read_string(char **str)
{
	char *field;

	if (cache_read_field(&field) == -1) {
		*str = NULL;
		return -1;
	}

	*str = (field[0] == '\0') ? NULL : xstrdup(field);
	return 0;
}

void
cache_write_entry(const struct track *t)
{
	cache_write_string(t->path);
	cache_write_string(t->artist);
	cache_write_string(t->album);
	cache_write_string(t->date);
	cache_write_string(t->discnumber);
	cache_write_string(t->tracknumber);
	cache_write_string(t->title);
	cache_write_number(t->duration);
	cache_write_string(t->genre);
}

static void
cache_write_number(unsigned int num)
{
	fprintf(cache_fp, "%u%c", num, '\0');
}

static void
cache_write_string(const char *str)
{
	if (str != NULL)
		fputs(str, cache_fp);
	putc('\0', cache_fp);
}
