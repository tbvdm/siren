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

#ifdef __OpenBSD__
#include <sys/queue.h>
#else
#include "compat/queue.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "siren.h"

struct history_entry {
	char			*line;
	TAILQ_ENTRY(history_entry) entries;
};

TAILQ_HEAD(history_list, history_entry);

struct history {
	struct history_list	 list;
	struct history_entry	*current_entry;
	unsigned int		 nentries;
};

static void history_remove(struct history *, struct history_entry *);

void
history_add(struct history *h, const char *line)
{
	struct history_entry	*e;
	unsigned int		 maxentries;

	maxentries = (unsigned int)option_get_number("max-history-entries");
	if (maxentries == 0)
		return;

	/* Don't add the entry if it is identical to the most recent one. */
	if ((e = TAILQ_FIRST(&h->list)) != NULL && !strcmp(e->line, line))
		return;

	/*
	 * Remove the oldest entry if we have reached the maximum number of
	 * entries.
	 */
	if (h->nentries == maxentries) {
		e = TAILQ_LAST(&h->list, history_list);
		history_remove(h, e);
	}

	e = xmalloc(sizeof *e);
	e->line = xstrdup(line);
	TAILQ_INSERT_HEAD(&h->list, e, entries);
	h->nentries++;
}

void
history_clear(struct history *h)
{
	struct history_entry *e;

	while ((e = TAILQ_FIRST(&h->list)) != NULL)
		history_remove(h, e);
}

void
history_free(struct history *h)
{
	history_clear(h);
	free(h);
}

const char *
history_get_next(struct history *h)
{
	if (h->current_entry == NULL) {
		if ((h->current_entry = TAILQ_FIRST(&h->list)) == NULL)
			/* History is empty. */
			return NULL;
	} else {
		if (h->current_entry == TAILQ_LAST(&h->list, history_list))
			/* End of history reached. */
			return NULL;
		h->current_entry = TAILQ_NEXT(h->current_entry, entries);
	}

	return h->current_entry->line;
}

const char *
history_get_prev(struct history *h)
{
	if (h->current_entry == NULL)
		return NULL;

	if ((h->current_entry = TAILQ_PREV(h->current_entry, history_list,
	    entries)) == NULL)
		return NULL;

	return h->current_entry->line;
}

struct history *
history_init(void)
{
	struct history *h;

	h = xmalloc(sizeof *h);
	TAILQ_INIT(&h->list);
	h->current_entry = NULL;
	h->nentries = 0;

	return h;
}

static void
history_remove(struct history *h, struct history_entry *e)
{
	TAILQ_REMOVE(&h->list, e, entries);
	free(e->line);
	free(e);
	h->nentries--;
}

void
history_resize(struct history *h, unsigned int maxentries)
{
	struct history_entry *e;

	while (h->nentries > maxentries) {
		e = TAILQ_LAST(&h->list, history_list);
		history_remove(h, e);
	}
}

void
history_rewind(struct history *h)
{
	h->current_entry = NULL;
}
