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

#include <limits.h>
#include <stdlib.h>

#include "siren.h"

#ifdef HAVE_QUEUE_H
#include <sys/queue.h>
#else
#include "compat/queue.h"
#endif

#define MENU_NENTRIES_MAX UINT_MAX

struct menu {
	struct menu_entry *active;
	struct menu_entry *selected;
	struct menu_entry *top;
	unsigned int	 nentries;

	void		 (*free_entry_data)(void *);
	void		 (*get_entry_text)(const void *, char *, size_t);
	int		 (*search_entry_data)(const void *, const char *);

	TAILQ_HEAD(menu_list, menu_entry) list;
};

struct menu_entry {
	unsigned int	 index;
	void		*data;
	TAILQ_ENTRY(menu_entry) entries;
};

void
menu_activate_entry(struct menu *m, struct menu_entry *e)
{
	m->active = e;
}

static void
menu_adjust_scroll_offset(struct menu *m)
{
	unsigned int nrows;

	if (m->nentries == 0)
		return;

	nrows = screen_view_get_nrows();

	/*
	 * If the selected entry is above the viewport, then move the selected
	 * entry to the top of the viewport.
	 */
	if (m->selected->index < m->top->index || nrows == 0)
		m->top = m->selected;
	/*
	 * If the selected entry is below the viewport, then move the selected
	 * entry to the bottom of the viewport.
	 */
	else if (m->selected->index >= m->top->index + nrows)
		do
			m->top = TAILQ_NEXT(m->top, entries);
		while (m->top->index != m->selected->index - nrows + 1);
	/*
	 * If the viewport extends below the last entry, then, if possible,
	 * move the last entry to the bottom of the viewport.
	 */
	else
		while (m->top->index > 0 &&
		    m->top->index + nrows > m->nentries)
			m->top = TAILQ_PREV(m->top, menu_list, entries);
}

void
menu_free(struct menu *m)
{
	menu_remove_all_entries(m);
	free(m);
}

struct menu_entry *
menu_get_active_entry(const struct menu *m)
{
	return m->active;
}

void *
menu_get_entry_data(const struct menu_entry *e)
{
	return e->data;
}

struct menu_entry *
menu_get_first_entry(const struct menu *m)
{
	return TAILQ_FIRST(&m->list);
}

struct menu_entry *
menu_get_last_entry(const struct menu *m)
{
	return TAILQ_LAST(&m->list, menu_list);
}

unsigned int
menu_get_nentries(const struct menu *m)
{
	return m->nentries;
}

struct menu_entry *
menu_get_next_entry(const struct menu_entry *e)
{
	return TAILQ_NEXT(e, entries);
}

struct menu_entry *
menu_get_prev_entry(const struct menu_entry *e)
{
	return TAILQ_PREV(e, menu_list, entries);
}

struct menu_entry *
menu_get_selected_entry(const struct menu *m)
{
	return m->selected;
}

void *
menu_get_selected_entry_data(const struct menu *m)
{
	return m->selected == NULL ? NULL : m->selected->data;
}

struct menu *
menu_init(void (*free_entry_data)(void *),
    void (*get_entry_text)(const void *, char *, size_t),
    int (*search_entry_data)(const void *, const char *))
{
	struct menu *m;

	m = xmalloc(sizeof *m);
	m->active = NULL;
	m->selected = NULL;
	m->top = NULL;
	m->nentries = 0;
	m->free_entry_data = free_entry_data;
	m->get_entry_text = get_entry_text;
	m->search_entry_data = search_entry_data;
	TAILQ_INIT(&m->list);
	return m;
}

void
menu_insert_after(struct menu *m, struct menu_entry *le, void *data)
{
	struct menu_entry *e;

	if (m->nentries == MENU_NENTRIES_MAX)
		return;

	e = xmalloc(sizeof *e);
	e->data = data;
	e->index = le->index + 1;

	TAILQ_INSERT_AFTER(&m->list, le, e, entries);
	m->nentries++;

	if (m->nentries == 1)
		/*
		 * This is the first entry in the menu: make it the top entry
		 * and the selected entry.
		 */
		m->top = m->selected = e;

	/* Increment the index of the entries after the inserted entry. */
	while ((e = TAILQ_NEXT(e, entries)) != NULL)
		e->index++;
}

void
menu_insert_before(struct menu *m, struct menu_entry *le, void *data)
{
	struct menu_entry *e;

	if (m->nentries == MENU_NENTRIES_MAX)
		return;

	e = xmalloc(sizeof *e);
	e->data = data;
	e->index = le->index;

	TAILQ_INSERT_BEFORE(le, e, entries);
	m->nentries++;

	if (m->nentries == 1)
		/*
		 * This is the first entry in the menu: make it the top entry
		 * and the selected entry.
		 */
		m->top = m->selected = e;

	/* Increment the index of the entries after the inserted entry. */
	while ((e = TAILQ_NEXT(e, entries)) != NULL)
		e->index++;
}

void
menu_insert_tail(struct menu *m, void *data)
{
	struct menu_entry *e;

	if (m->nentries == MENU_NENTRIES_MAX)
		return;

	e = xmalloc(sizeof *e);
	e->data = data;
	e->index = m->nentries;

	TAILQ_INSERT_TAIL(&m->list, e, entries);
	m->nentries++;

	if (m->nentries == 1)
		/*
		 * This is the first entry in the menu: make it the top entry
		 * and the selected entry.
		 */
		m->top = m->selected = e;
}

/* Move entry e before entry be. */
void
menu_move_entry_before(struct menu *m, struct menu_entry *be,
    struct menu_entry *e)
{
	struct menu_entry *f;

	/* Update the index of each relevant entry. */
	e->index = be->index;
	for (f = be; f != e; f = TAILQ_NEXT(f, entries))
		f->index++;

	/* Move the entry to its new position. */
	TAILQ_REMOVE(&m->list, e, entries);
	TAILQ_INSERT_BEFORE(be, e, entries);
}

void
menu_move_entry_down(struct menu *m, struct menu_entry *e)
{
	struct menu_entry *f;

	if ((f = TAILQ_NEXT(e, entries)) != NULL)
		menu_move_entry_before(m, e, f);
}

void
menu_move_entry_up(struct menu *m, struct menu_entry *e)
{
	struct menu_entry *f;

	if ((f = TAILQ_PREV(e, menu_list, entries)) != NULL)
		menu_move_entry_before(m, f, e);
}

void
menu_print(struct menu *m)
{
	struct menu_entry	*e;
	unsigned int		 bottomrow, nrows, percent, toprow;
	size_t			 bufsize;
	char			*buf;

	menu_adjust_scroll_offset(m);

	nrows = screen_view_get_nrows();
	if (m->nentries == 0) {
		toprow = 0;
		bottomrow = 0;
		percent = 100;
	} else {
		toprow = m->top->index + 1;
		if (nrows == 0) {
			bottomrow = 0;
			percent = 100 * toprow / m->nentries;
		} else {
			if (m->nentries < nrows)
				bottomrow = m->nentries;
			else
				bottomrow = toprow + nrows - 1;
			percent = 100 * bottomrow / m->nentries;
		}
	}
	screen_view_title_printf_right(" %u-%u/%u (%u%%)", toprow, bottomrow,
	    m->nentries, percent);

	screen_view_print_begin();
	if (m->nentries > 0) {
		bufsize = screen_get_ncols() + 1;
		buf = xmalloc(bufsize);
		e = m->top;
		while (nrows-- > 0 && e != NULL) {
			m->get_entry_text(e->data, buf, bufsize);
			if (e == m->selected)
				screen_view_print_selected(buf);
			else if (e == m->active)
				screen_view_print_active(buf);
			else
				screen_view_print(buf);
			e = TAILQ_NEXT(e, entries);
		}
		free(buf);
	}
	screen_view_print_end();
}

void
menu_remove_all_entries(struct menu *m)
{
	struct menu_entry *e;

	while ((e = TAILQ_FIRST(&m->list)) != NULL) {
		TAILQ_REMOVE(&m->list, e, entries);
		if (m->free_entry_data != NULL)
			m->free_entry_data(e->data);
		free(e);
	}

	m->active = NULL;
	m->selected = NULL;
	m->top = NULL;
	m->nentries = 0;
}

void
menu_remove_entry(struct menu *m, struct menu_entry *e)
{
	struct menu_entry *f;

	if (m->active == e)
		m->active = NULL;
	if (m->top == e)
		m->top = TAILQ_NEXT(m->top, entries);
	if (m->selected == e) {
		if (TAILQ_NEXT(m->selected, entries) != NULL)
			m->selected = TAILQ_NEXT(m->selected, entries);
		else
			m->selected = TAILQ_PREV(m->selected, menu_list,
			    entries);
	}

	/* Decrement the index of the entries after the specified entry. */
	f = e;
	while ((f = TAILQ_NEXT(f, entries)) != NULL)
		f->index--;

	TAILQ_REMOVE(&m->list, e, entries);
	m->nentries--;

	if (m->free_entry_data != NULL)
		m->free_entry_data(e->data);
	free(e);
}

void
menu_remove_selected_entry(struct menu *m)
{
	if (m->selected != NULL)
		menu_remove_entry(m, m->selected);
}

void
menu_scroll_down(struct menu *m, enum menu_scroll scroll)
{
	unsigned int nrows, nscroll;

	if (m->nentries == 0)
		return;

	nrows = screen_view_get_nrows();
	switch (scroll) {
	case MENU_SCROLL_HALF_PAGE:
		nscroll = (nrows + 1) / 2;
		break;
	case MENU_SCROLL_PAGE:
		nscroll = nrows;
		break;
	case MENU_SCROLL_LINE:
	default:
		nscroll = 1;
		break;
	}

	if (m->top->index + nrows >= m->nentries)
		/*
		 * The last entry already is visible, so we cannot scroll down
		 * farther. Select the last entry instead.
		 */
		m->selected = TAILQ_LAST(&m->list, menu_list);
	else {
		/*
		 * Scroll down the requested number of lines or just as far as
		 * possible.
		 */
		while (nscroll-- > 0 && m->top->index + nrows < m->nentries)
			m->top = TAILQ_NEXT(m->top, entries);

		/*
		 * Select the top entry if the selected entry is no longer
		 * visible.
		 */
		if (m->selected->index < m->top->index)
			m->selected = m->top;
	}
}

void
menu_scroll_up(struct menu *m, enum menu_scroll scroll)
{
	unsigned int nrows, nscroll;

	if (m->nentries == 0)
		return;

	nrows = screen_view_get_nrows();
	switch (scroll) {
	case MENU_SCROLL_HALF_PAGE:
		nscroll = (nrows + 1) / 2;
		break;
	case MENU_SCROLL_PAGE:
		nscroll = nrows;
		break;
	case MENU_SCROLL_LINE:
	default:
		nscroll = 1;
		break;
	}

	if (m->top->index == 0)
		/*
		 * The first entry already is visible, so we cannot scroll up
		 * farther. Select the first entry instead.
		 */
		m->selected = TAILQ_FIRST(&m->list);
	else {
		/*
		 * Scroll up the requested number of lines or just as far as
		 * possible.
		 */
		while (nscroll-- > 0 && m->top->index > 0)
			m->top = TAILQ_PREV(m->top, menu_list, entries);

		/*
		 * Select the bottom entry if the selected entry is no longer
		 * visible.
		 */
		while (m->selected->index >= m->top->index + nrows)
			m->selected = TAILQ_PREV(m->selected, menu_list,
			    entries);
	}
}

void
menu_search_next(struct menu *m, const char *s)
{
	struct menu_entry *e;

	if (m->selected != NULL && m->search_entry_data != NULL) {
		e = m->selected;
		do {
			if (TAILQ_NEXT(e, entries) != NULL)
				e = TAILQ_NEXT(e, entries);
			else {
				e = TAILQ_FIRST(&m->list);
				msg_info("Search wrapped to top");
			}

			if (m->search_entry_data(e->data, s) == 0) {
				m->selected = e;
				return;
			}
		} while (e != m->selected);
	}

	msg_errx("Not found");
}

void
menu_search_prev(struct menu *m, const char *s)
{
	struct menu_entry *e;

	if (m->selected != NULL && m->search_entry_data != NULL) {
		e = m->selected;
		do {
			if (TAILQ_PREV(e, menu_list, entries) != NULL)
				e = TAILQ_PREV(e, menu_list, entries);
			else {
				e = TAILQ_LAST(&m->list, menu_list);
				msg_info("Search wrapped to bottom");
			}

			if (m->search_entry_data(e->data, s) == 0) {
				m->selected = e;
				return;
			}
		} while (e != m->selected);
	}

	msg_errx("Not found");
}

void
menu_select_active_entry(struct menu *m)
{
	if (m->active != NULL)
		m->selected = m->active;
}

void
menu_select_entry(struct menu *m, struct menu_entry *e)
{
	m->selected = e;
}

void
menu_select_first_entry(struct menu *m)
{
	m->selected = TAILQ_FIRST(&m->list);
}

void
menu_select_last_entry(struct menu *m)
{
	m->selected = TAILQ_LAST(&m->list, menu_list);
}

void
menu_select_next_entry(struct menu *m)
{
	if (m->selected != NULL && TAILQ_NEXT(m->selected, entries) != NULL)
		m->selected = TAILQ_NEXT(m->selected, entries);
}

void
menu_select_prev_entry(struct menu *m)
{
	if (m->selected != NULL &&
	    TAILQ_PREV(m->selected, menu_list, entries) != NULL)
		m->selected = TAILQ_PREV(m->selected, menu_list, entries);
}
