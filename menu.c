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

#define MENU_NENTRIES_MAX	(UINT_MAX - 1)
#define MENU_NONE		UINT_MAX

struct menu {
	unsigned int	 active_index;
	unsigned int	 sel_index;
	unsigned int	 nentries;
	unsigned int	 scroll_offset;

	void		 (*free_entry_data)(void *);
	void		 (*get_entry_text)(const void *, char *, size_t);
	int		 (*search_entry_data)(const void *, const char *);

	TAILQ_HEAD(menu_list, menu_entry) list;
};

struct menu_entry {
	void		*data;
	TAILQ_ENTRY(menu_entry) entries;
};

static struct menu_entry *menu_get_entry_at_index(const struct menu *,
    unsigned int);

void
menu_activate_entry(struct menu *m, struct menu_entry *ae)
{
	struct menu_entry	*e;
	unsigned int		 i;

	/*
	 * Determine the index of the specified entry. This process also allows
	 * us to ensure that the specified entry really exists in the menu.
	 */
	i = 0;
	TAILQ_FOREACH(e, &m->list, entries) {
		if (e == ae) {
			m->active_index = i;
			break;
		}
		i++;
	}
}

static void
menu_adjust_scroll_offset(struct menu *m)
{
	unsigned int nrows;

	nrows = screen_view_get_nrows();

	/* The selected entry is above the viewport: scroll up. */
	if (m->sel_index < m->scroll_offset || nrows == 0)
		m->scroll_offset = m->sel_index;

	/* The selected entry is below the viewport: scroll down. */
	else if (m->sel_index >= m->scroll_offset + nrows)
		m->scroll_offset = m->sel_index - nrows + 1;

	/* The viewport extends below the last entry: scroll up if possible. */
	else if (m->scroll_offset && m->scroll_offset + nrows > m->nentries) {
		if (m->nentries <= nrows)
			m->scroll_offset = 0;
		else
			m->scroll_offset = m->nentries - nrows;
	}
}

void
menu_clear(struct menu *m)
{
	struct menu_entry *e;

	while ((e = TAILQ_FIRST(&m->list)) != NULL) {
		TAILQ_REMOVE(&m->list, e, entries);
		if (m->free_entry_data != NULL)
			m->free_entry_data(e->data);
		free(e);
	}

	m->sel_index = 0;
	m->nentries = 0;
	m->scroll_offset = 0;
}

void
menu_free(struct menu *m)
{
	menu_clear(m);
	free(m);
}

struct menu_entry *
menu_get_active_entry(const struct menu *m)
{
	if (m->active_index == MENU_NONE)
		return NULL;
	return menu_get_entry_at_index(m, m->active_index);
}

static struct menu_entry *
menu_get_entry_at_index(const struct menu *m, unsigned int index)
{
	struct menu_entry *e;

	TAILQ_FOREACH(e, &m->list, entries)
		if (index-- == 0)
			break;

	return e;
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
	return menu_get_entry_at_index(m, m->sel_index);
}

void *
menu_get_selected_entry_data(const struct menu *m)
{
	struct menu_entry *e;

	if ((e = menu_get_selected_entry(m)) == NULL)
		return NULL;

	return e->data;
}

struct menu *
menu_init(void (*free_entry_data)(void *),
    void (*get_entry_text)(const void *, char *, size_t),
    int (*search_entry_data)(const void *, const char *))
{
	struct menu *m;

	m = xmalloc(sizeof *m);

	m->active_index = MENU_NONE;
	m->sel_index = 0;
	m->nentries = 0;
	m->scroll_offset = 0;

	m->free_entry_data = free_entry_data;
	m->get_entry_text = get_entry_text;
	m->search_entry_data = search_entry_data;

	TAILQ_INIT(&m->list);

	return m;
}

void
menu_insert_before(struct menu *m, struct menu_entry *le, void *data)
{
	struct menu_entry	*e, *f;
	unsigned int		 i;

	if (m->nentries == MENU_NENTRIES_MAX)
		return;

	e = xmalloc(sizeof *e);
	e->data = data;

	TAILQ_INSERT_BEFORE(le, e, entries);
	m->nentries++;

	/*
	 * If the new entry is inserted before the selected entry, then we have
	 * to increment the index of the selected entry.
	 */
	i = 0;
	TAILQ_FOREACH(f, &m->list, entries) {
		if (f == e) {
			m->sel_index++;
			break;
		}
		if (i == m->sel_index)
			break;
		i++;
	}
}

void
menu_insert_tail(struct menu *m, void *data)
{
	struct menu_entry *e;

	if (m->nentries == MENU_NENTRIES_MAX)
		return;

	e = xmalloc(sizeof *e);
	e->data = data;

	TAILQ_INSERT_TAIL(&m->list, e, entries);
	m->nentries++;
}

void
menu_move_entry_down(struct menu_entry *e)
{
	struct menu_entry	*f;
	void			*data;

	if ((f = TAILQ_NEXT(e, entries)) != NULL) {
		data = e->data;
		e->data = f->data;
		f->data = data;
	}
}

void
menu_move_entry_up(struct menu_entry *e)
{
	struct menu_entry	*f;
	void			*data;

	if ((f = TAILQ_PREV(e, menu_list, entries)) != NULL) {
		data = e->data;
		e->data = f->data;
		f->data = data;
	}
}

void
menu_print(struct menu *m)
{
	struct menu_entry	*e;
	unsigned int		 bottomrow, bufsize, i, nrows, percent, toprow;
	char			*buf;

	menu_adjust_scroll_offset(m);

	nrows = screen_view_get_nrows();
	if (m->nentries == 0) {
		toprow = 0;
		bottomrow = 0;
		percent = 100;
	} else if (nrows == 0) {
		toprow = m->scroll_offset + 1;
		bottomrow = 0;
		percent = 100 * toprow / m->nentries;
	} else {
		toprow = m->scroll_offset + 1;
		bottomrow = m->nentries < nrows ? m->nentries :
		    m->scroll_offset + nrows;
		percent = 100 * bottomrow / m->nentries;
	}

	screen_view_title_printf_right(" %u-%u/%u (%u%%)", toprow, bottomrow,
	    m->nentries, percent);

	bufsize = screen_get_ncols() + 1;
	buf = xmalloc(bufsize);

	screen_view_print_begin();

	/* Print entries. */
	e = menu_get_entry_at_index(m, m->scroll_offset);
	for (i = 0; i < nrows && e != NULL; i++) {
		m->get_entry_text(e->data, buf, bufsize);
		screen_view_move_cursor(i);

		if (i == m->sel_index - m->scroll_offset)
			screen_view_print_selected(buf);
		else if (i == m->active_index - m->scroll_offset)
			screen_view_print_active(buf);
		else
			screen_view_print(buf);

		e = TAILQ_NEXT(e, entries);
	}

	/* Move the cursor back to the selected entry. */
	screen_view_move_cursor(m->sel_index - m->scroll_offset);

	screen_view_print_end();
	free(buf);
}

static void
menu_remove_entry(struct menu *m, struct menu_entry *e)
{
	TAILQ_REMOVE(&m->list, e, entries);
	if (m->free_entry_data != NULL)
		m->free_entry_data(e->data);
	free(e);
	m->nentries--;
}

void
menu_remove_first_entry(struct menu *m)
{
	struct menu_entry *e;

	if ((e = TAILQ_FIRST(&m->list)) != NULL) {
		menu_remove_entry(m, e);
		if (m->sel_index > 0)
			m->sel_index--;
	}
}

void
menu_remove_selected_entry(struct menu *m)
{
	struct menu_entry *e;

	if ((e = menu_get_entry_at_index(m, m->sel_index)) != NULL) {
		menu_remove_entry(m, e);
		if (m->active_index == m->sel_index)
			m->active_index = MENU_NONE;
		if (m->sel_index > 0 && m->sel_index == m->nentries)
			m->sel_index--;
	}
}

void
menu_scroll_down(struct menu *m, enum menu_scroll scroll)
{
	unsigned int nrows, nscroll;

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

	if (m->scroll_offset + nrows >= m->nentries) {
		/*
		 * The last entry already is visible, so we cannot scroll down
		 * farther. Select the last entry instead.
		 */
		if (m->nentries)
			m->sel_index = m->nentries - 1;
	} else {
		/*
		 * Check if we can scroll the requested number of lines. If we
		 * can't, just scroll as far as we can.
		 */
		if (m->scroll_offset + nrows + nscroll < m->nentries)
			m->scroll_offset += nscroll;
		else
			m->scroll_offset = m->nentries - nrows;

		if (m->sel_index < m->scroll_offset)
			m->sel_index = m->scroll_offset;
	}
}

void
menu_scroll_up(struct menu *m, enum menu_scroll scroll)
{
	unsigned int nrows, nscroll;

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

	if (m->scroll_offset == 0)
		/*
		 * The first entry already is visible, so we cannot scroll up
		 * farther. Select the first entry instead.
		 */
		m->sel_index = 0;
	else {
		/*
		 * Check if we can scroll the requested number of lines. If we
		 * can't, just scroll as far as we can.
		 */
		if (m->scroll_offset > nscroll)
			m->scroll_offset -= nscroll;
		else
			m->scroll_offset = 0;

		if (nrows && m->sel_index >= m->scroll_offset + nrows)
			m->sel_index = m->scroll_offset + nrows - 1;
	}
}

void
menu_search_next(struct menu *m, const char *s)
{
	struct menu_entry	*e;
	unsigned int		 i;

	if (m->nentries == 0 || m->search_entry_data == NULL)
		return;

	if (m->sel_index + 1 < m->nentries) {
		i = m->sel_index + 1;
		e = menu_get_entry_at_index(m, i);
		do {
			if (m->search_entry_data(e->data, s) == 0) {
				m->sel_index = i;
				return;
			}
			e = TAILQ_NEXT(e, entries);
		} while (++i < m->nentries);
	}

	i = 0;
	e = menu_get_entry_at_index(m, i);
	do {
		if (m->search_entry_data(e->data, s) == 0) {
			m->sel_index = i;
			msg_info("Search wrapped to top");
			return;
		}
		e = TAILQ_NEXT(e, entries);
	} while (++i < m->nentries);

	msg_errx("Not found");
}

void
menu_search_prev(struct menu *m, const char *s)
{
	struct menu_entry	*e;
	unsigned int		 i;

	if (m->nentries == 0 || m->search_entry_data == NULL)
		return;

	if (m->sel_index > 0) {
		i = m->sel_index - 1;
		e = menu_get_entry_at_index(m, i);
		do {
			if (m->search_entry_data(e->data, s) == 0) {
				m->sel_index = i;
				return;
			}
			e = TAILQ_PREV(e, menu_list, entries);
		} while (i-- > 0);
	}

	i = m->nentries - 1;
	e = menu_get_entry_at_index(m, i);
	do {
		if (m->search_entry_data(e->data, s) == 0) {
			m->sel_index = i;
			msg_info("Search wrapped to bottom");
			return;
		}
		e = TAILQ_PREV(e, menu_list, entries);
	} while (i-- > 0);

	msg_errx("Not found");
}

void
menu_select_active_entry(struct menu *m)
{
	if (m->active_index != MENU_NONE)
		m->sel_index = m->active_index;
}

void
menu_select_entry(struct menu *m, struct menu_entry *se)
{
	struct menu_entry	*e;
	unsigned int		 i;

	/*
	 * Determine the index of the specified entry. This process also allows
	 * us to ensure that the specified entry really exists in the menu.
	 */
	i = 0;
	TAILQ_FOREACH(e, &m->list, entries) {
		if (e == se) {
			m->sel_index = i;
			break;
		}
		i++;
	}
}

void
menu_select_first_entry(struct menu *m)
{
	m->sel_index = 0;
	m->scroll_offset = 0;
}

void
menu_select_last_entry(struct menu *m)
{
	if (m->nentries > 0)
		m->sel_index = m->nentries - 1;
}

void
menu_select_next_entry(struct menu *m)
{
	if (m->sel_index + 1 < m->nentries)
		m->sel_index++;
}

void
menu_select_prev_entry(struct menu *m)
{
	if (m->sel_index > 0)
		m->sel_index--;
}
