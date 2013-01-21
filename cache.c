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

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "siren.h"

#ifdef HAVE_TREE_H
#include <sys/tree.h>
#else
#include "compat/tree.h"
#endif

#define CACHE_BUFSIZE	4096
#define CACHE_VERSION	0

struct cache_entry {
	char		*path;
	char		*album;
	char		*artist;
	char		*date;
	char		*genre;
	char		*title;
	char		*tracknumber;
	unsigned int	 duration;
	RB_ENTRY(cache_entry) entries;
};

RB_HEAD(cache_tree, cache_entry);

static int		 cache_cmp_entry(struct cache_entry *,
			    struct cache_entry *);
static void		 cache_free_entry(struct cache_entry *);
static int		 cache_read_file(void);
static int		 cache_read_number(unsigned int *);
static int		 cache_read_string(char **);
static void		 cache_remove_entry(struct cache_entry *);
static void		 cache_write_number(unsigned int);
static void		 cache_write_string(const char *);

RB_PROTOTYPE(cache_tree, cache_entry, entries, cache_cmp_entry)

static struct cache_tree cache_tree = RB_INITIALIZER(cache_tree);
static FILE		*cache_fp;
static size_t		 cache_bufidx;
static size_t		 cache_buflen;
static size_t		 cache_bufsize;
static int		 cache_modified;
static char		*cache_buf;
static char		*cache_file;

RB_GENERATE(cache_tree, cache_entry, entries, cache_cmp_entry)

static void
cache_add_entry(struct cache_entry *e)
{
	if (RB_INSERT(cache_tree, &cache_tree, e) != NULL) {
		/* This should not happen. */
		LOG_ERRX("%s: track is already in cache", e->path);
		cache_free_entry(e);
	}
}

void
cache_add_metadata(const struct track *t)
{
	struct cache_entry *e;

	e = xmalloc(sizeof *e);
	e->path = xstrdup(t->path);
	e->album = t->album == NULL ? NULL : xstrdup(t->album);
	e->artist = t->artist == NULL ? NULL : xstrdup(t->artist);
	e->date = t->date == NULL ? NULL : xstrdup(t->date);
	e->genre = t->genre == NULL ? NULL : xstrdup(t->genre);
	e->title = t->title == NULL ? NULL : xstrdup(t->title);
	e->tracknumber = t->tracknumber == NULL ? NULL :
	    xstrdup(t->tracknumber);
	e->duration = t->duration;

	cache_add_entry(e);
	cache_modified = 1;
}

void
cache_clear(void)
{
	struct cache_entry *e;

	while ((e = RB_ROOT(&cache_tree)) != NULL)
		cache_remove_entry(e);
	cache_modified = 1;
}

static int
cache_cmp_entry(struct cache_entry *e1, struct cache_entry *e2)
{
	return strcmp(e1->path, e2->path);
}

void
cache_end(void)
{
	if (cache_modified)
		(void)cache_write_file();

	cache_clear();
	free(cache_file);
}

static void
cache_free_entry(struct cache_entry *e)
{
	free(e->path);
	free(e->album);
	free(e->artist);
	free(e->date);
	free(e->genre);
	free(e->title);
	free(e->tracknumber);
	free(e);
}

int
cache_get_metadata(struct track *t)
{
	struct cache_entry *find, search;

	search.path = t->path;
	find = RB_FIND(cache_tree, &cache_tree, &search);
	if (find == NULL)
		return -1;

	t->album = find->album == NULL ? NULL : xstrdup(find->album);
	t->artist = find->artist == NULL ? NULL : xstrdup(find->artist);
	t->date = find->date == NULL ? NULL : xstrdup(find->date);
	t->genre = find->genre == NULL ? NULL : xstrdup(find->genre);
	t->title = find->title == NULL ? NULL : xstrdup(find->title);
	t->tracknumber = find->tracknumber == NULL ? NULL :
	    xstrdup(find->tracknumber);
	t->duration = find->duration;

	return 0;
}

void
cache_init(void)
{
	cache_file = conf_get_path(CACHE_FILE);
	if (cache_read_file() == -1)
		msg_err("Cannot read metadata cache");
}

static int
cache_read_field(char **field)
{
	size_t	 fieldlen;
	char	*sep;

	for (;;) {
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

		sep = memchr(cache_buf + cache_bufidx, '\0',
		    cache_buflen - cache_bufidx);
		if (sep != NULL)
			break;

		if (feof(cache_fp)) {
			LOG_ERRX("no field separator at EOF");
			return -1;
		}

		if (cache_bufidx > 0) {
			(void)memmove(cache_buf, cache_buf + cache_bufidx,
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
cache_read_file(void)
{
	struct cache_entry	*e;
	int			 ret;
	unsigned int		 version;

	cache_fp = fopen(cache_file, "r");
	if (cache_fp == NULL)
		return -1;

	cache_bufidx = 0;
	cache_buflen = 0;
	cache_bufsize = CACHE_BUFSIZE;
	cache_buf = xmalloc(cache_bufsize);

	if (cache_read_number(&version) == -1) {
		free(cache_buf);
		(void)fclose(cache_fp);
		return -1;
	}

	if (version != CACHE_VERSION) {
		LOG_ERRX("%u: unsupported metadata cache version", version);
		msg_errx("Unsupported metadata cache version");
		free(cache_buf);
		(void)fclose(cache_fp);
		return -1;
	}

	for (;;) {
		e = xmalloc(sizeof *e);

		ret = 0;
		ret += cache_read_string(&e->path);
		ret += cache_read_string(&e->artist);
		ret += cache_read_string(&e->album);
		ret += cache_read_string(&e->date);
		ret += cache_read_string(&e->tracknumber);
		ret += cache_read_string(&e->title);
		ret += cache_read_number(&e->duration);
		ret += cache_read_string(&e->genre);

		/* Check for EOF or error. */
		if (ret) {
			cache_free_entry(e);
			break;
		}

		cache_add_entry(e);
	}

	free(cache_buf);
	(void)fclose(cache_fp);
	return 0;
}

static int
cache_read_number(unsigned int *num)
{
	char		*field;
	const char	*errstr;

	if (cache_read_field(&field) == -1)
		return -1;

	*num = (unsigned int)strtonum(field, 0, UINT_MAX, &errstr);
	if (errstr != NULL)
		LOG_ERRX("%s: number is %s", field, errstr);

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

	if (field[0] == '\0')
		*str = NULL;
	else
		*str = xstrdup(field);

	return 0;
}

static void
cache_remove_entry(struct cache_entry *e)
{
	(void)RB_REMOVE(cache_tree, &cache_tree, e);
	cache_free_entry(e);
}

int
cache_write_file(void)
{
	struct cache_entry *e;

	cache_fp = fopen(cache_file, "w");
	if (cache_fp == NULL) {
		LOG_ERR("fopen: %s", cache_file);
		return -1;
	}

	cache_write_number(CACHE_VERSION);
	RB_FOREACH(e, cache_tree, &cache_tree) {
		cache_write_string(e->path);
		cache_write_string(e->artist);
		cache_write_string(e->album);
		cache_write_string(e->date);
		cache_write_string(e->tracknumber);
		cache_write_string(e->title);
		cache_write_number(e->duration);
		cache_write_string(e->genre);
	}

	(void)fclose(cache_fp);
	cache_modified = 0;
	return 0;
}

static void
cache_write_number(unsigned int num)
{
	(void)fprintf(cache_fp, "%u%c", num, '\0');
}

static void
cache_write_string(const char *str)
{
	if (str != NULL)
		(void)fputs(str, cache_fp);
	(void)putc('\0', cache_fp);
}
