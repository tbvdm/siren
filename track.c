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

/* Let glibc expose strcasestr(). */
#define _GNU_SOURCE

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "siren.h"

static int track_cmp_number(const char *, const char *);
static int track_cmp_string(const char *, const char *);

int
track_cmp(const struct track *t1, const struct track *t2)
{
	int ret;

	if ((ret = track_cmp_string(t1->artist, t2->artist)))
		return ret;

	if ((ret = track_cmp_number(t1->date, t2->date)))
		return ret;

	if ((ret = track_cmp_string(t1->album, t2->album)))
		return ret;

	if ((ret = track_cmp_number(t1->tracknumber, t2->tracknumber)))
		return ret;

	if ((ret = track_cmp_string(t1->title, t2->title)))
		return ret;

	return strcmp(t1->path, t2->path);
}

static int
track_cmp_number(const char *s1, const char *s2)
{
	int		 i1, i2;
	const char	*errstr;

	if (s1 == NULL)
		return s2 == NULL ? 0 : -1;
	if (s2 == NULL)
		return 1;

	i1 = (int)strtonum(s1, 0, INT_MAX, &errstr);
	if (errstr != NULL)
		/*
		 * Numerical comparison failed; fall back to a lexicographical
		 * comparison.
		 */
		return track_cmp_string(s1, s2);

	i2 = (int)strtonum(s2, 0, INT_MAX, &errstr);
	if (errstr != NULL)
		return track_cmp_string(s1, s2);

	return i1 < i2 ? -1 : i1 > i2;
}

static int
track_cmp_string(const char *s1, const char *s2)
{
	if (s1 == NULL)
		return s2 == NULL ? 0: -1;
	if (s2 == NULL)
		return 1;
	return strcasecmp(s1, s2);
}

void
track_free(struct track *t)
{
	if (t != NULL && --t->nrefs == 0) {
		free(t->album);
		free(t->artist);
		free(t->date);
		free(t->genre);
		free(t->path);
		free(t->title);
		free(t->tracknumber);
		free(t);
	}
}

void
track_hold(struct track *t)
{
	t->nrefs++;
}

struct track *
track_init(const char *path, const struct ip *ip)
{
	struct track	*t;
	int		 ret;
	char		*error;

	if (access(path, R_OK) == -1) {
		msg_err("%s", path);
		return NULL;
	}

	if (ip == NULL && (ip = plugin_find_ip(path)) == NULL) {
		msg_errx("%s: Unsupported file format", path);
		return NULL;
	}

	t = xmalloc(sizeof *t);
	t->path = xstrdup(path);
	t->ip = ip;
	t->ipdata = NULL;
	t->nrefs = 1;

	t->album = NULL;
	t->artist = NULL;
	t->date = NULL;
	t->genre = NULL;
	t->title = NULL;
	t->tracknumber = NULL;
	t->duration = 0;

	if (cache_get_metadata(t) == 0)
		return t;

	error = NULL;
	if ((ret = t->ip->get_metadata(t, &error)) != 0) {
		msg_ip_err(ret, error, "%s: Cannot read metadata", path);
		free(error);
		free(t);
		return NULL;
	}

	cache_add_metadata(t);

	return t;
}

int
track_search(const struct track *t, const char *search)
{
	if (t->album && strcasestr(t->album, search))
		return 0;
	if (t->artist && strcasestr(t->artist, search))
		return 0;
	if (t->date && strcasestr(t->date, search))
		return 0;
	if (t->genre && strcasestr(t->genre, search))
		return 0;
	if (t->title && strcasestr(t->title, search))
		return 0;
	if (t->tracknumber && strcasestr(t->tracknumber, search))
		return 0;
	if (strcasestr(t->path, search))
		return 0;
	return -1;
}
