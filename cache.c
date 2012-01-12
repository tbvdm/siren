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

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "siren.h"

#ifdef HAVE_TREE_H
#include <sys/tree.h>
#else
#include "compat/tree.h"
#endif

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
static int		 cache_read_number(char **, char *, unsigned int *);
static int		 cache_read_string(char **, char *, char **);
static void		 cache_remove_entry(struct cache_entry *);

RB_PROTOTYPE(cache_tree, cache_entry, entries, cache_cmp_entry)

static struct cache_tree cache_tree = RB_INITIALIZER(cache_tree);
static int		 cache_modified;
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
	e->album = xstrdup(t->album == NULL ? "" : t->album);
	e->artist = xstrdup(t->artist == NULL ? "" : t->artist);
	e->date = xstrdup(t->date == NULL ? "" : t->date);
	e->genre = xstrdup(t->genre == NULL ? "" : t->genre);
	e->title = xstrdup(t->title == NULL ? "" : t->title);
	e->tracknumber = xstrdup(t->tracknumber == NULL ? "" :
	    t->tracknumber);
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
	if ((find = RB_FIND(cache_tree, &cache_tree, &search)) == NULL)
		return -1;

	t->album = xstrdup(find->album);
	t->artist = xstrdup(find->artist);
	t->date = xstrdup(find->date);
	t->genre = xstrdup(find->genre);
	t->title = xstrdup(find->title);
	t->tracknumber = xstrdup(find->tracknumber);
	t->duration = find->duration;
	return 0;
}

void
cache_init(void)
{
	cache_file = conf_path(CACHE_FILE);
	if (cache_read_file() == -1)
		msg_err("Cannot read metadata cache");
}

static void
cache_read_entries(char *data, char *end)
{
	struct cache_entry	*e;
	int			 ret;
	unsigned int		 version;

	if (cache_read_number(&data, end, &version) == -1)
		return;

	if (version != CACHE_VERSION) {
		LOG_ERRX("%u: unsupported metadata cache version", version);
		return;
	}

	for (;;) {
		e = xmalloc(sizeof *e);
		ret = 0;
		ret += cache_read_string(&data, end, &e->path);
		ret += cache_read_string(&data, end, &e->artist);
		ret += cache_read_string(&data, end, &e->album);
		ret += cache_read_string(&data, end, &e->date);
		ret += cache_read_string(&data, end, &e->tracknumber);
		ret += cache_read_string(&data, end, &e->title);
		ret += cache_read_number(&data, end, &e->duration);
		ret += cache_read_string(&data, end, &e->genre);

		/* Check for EOF or error. */
		if (ret) {
			cache_free_entry(e);
			break;
		}

		cache_add_entry(e);
	}
}

static int
cache_read_field(char **data, char *end, char **field)
{
	*field = *data;

	/* Find end of field. */
	while (*data < end)
		if (*(*data)++ == '\0')
			return 0;

	/* EOF or field not NUL-terminated. */
	return -1;
}

static int
cache_read_file(void)
{
	struct stat	 sb;
	int		 fd;
	char		*data;

	if ((fd = open(cache_file, O_RDONLY)) == -1) {
		LOG_ERR("open: %s", cache_file);
		return -1;
	}

	if (fstat(fd, &sb) == -1) {
		LOG_ERR("fstat: %s", cache_file);
		(void)close(fd);
		return -1;
	}

	if (sb.st_size == 0) {
		(void)close(fd);
		return 0;
	}

	if ((data = mmap(0, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) ==
	    MAP_FAILED) {
		LOG_ERR("mmap: %s", cache_file);
		(void)close(fd);
		return -1;
	}

	cache_read_entries(data, data + sb.st_size);

	(void)munmap(data, sb.st_size);
	(void)close(fd);
	return 0;
}

static int
cache_read_number(char **data, char *end, unsigned int *num)
{
	char		*field;
	const char	*errstr;

	if (cache_read_field(data, end, &field) == -1)
		return -1;

	*num = (unsigned int)strtonum(field, 0, UINT_MAX, &errstr);
	if (errstr != NULL)
		LOG_ERRX("%s: number is %s", field, errstr);

	return 0;
}

static int
cache_read_string(char **data, char *end, char **str)
{
	char *field;

	if (cache_read_field(data, end, &field) == -1) {
		*str = NULL;
		return -1;
	}

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
	struct cache_entry	*e;
	FILE			*fp;

	if ((fp = fopen(cache_file, "w")) == NULL) {
		LOG_ERR("fopen: %s", cache_file);
		return -1;
	}

	(void)fprintf(fp, "%u%c", CACHE_VERSION, '\0');
	RB_FOREACH(e, cache_tree, &cache_tree)
		(void)fprintf(fp, "%s%c%s%c%s%c%s%c%s%c%s%c%u%c%s%c",
		    e->path, '\0',
		    e->artist, '\0',
		    e->album, '\0',
		    e->date, '\0',
		    e->tracknumber, '\0',
		    e->title, '\0',
		    e->duration, '\0',
		    e->genre, '\0');

	(void)fclose(fp);
	cache_modified = 0;
	return 0;
}
