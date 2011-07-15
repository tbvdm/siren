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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "siren.h"

static void		 queue_free_entry(void *);
static void		 queue_get_entry_text(const void *, char *, size_t);
static int		 queue_search_entry(const void *, const char *);

static pthread_mutex_t	 queue_menu_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct menu	*queue_menu;
static unsigned int	 queue_duration;
static char		*queue_print_format;

void
queue_activate_entry(void)
{
	struct track *t;

	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	if ((t = menu_get_selected_entry_data(queue_menu)) != NULL) {
		track_hold(t);
		player_play_track(t);
		queue_duration -= t->duration;
		menu_remove_selected_entry(queue_menu);
	}
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

void
queue_add_track(struct track *t)
{
	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	menu_insert_tail(queue_menu, t);
	queue_duration += t->duration;
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

void
queue_copy_entry(enum view_id view)
{
	struct track *t;

	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	if ((t = menu_get_selected_entry_data(queue_menu)) != NULL)
		view_copy_track(VIEW_ID_QUEUE, view, t);
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
}

void
queue_delete_all_entries(void)
{
	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	menu_clear(queue_menu);
	queue_duration = 0;
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

void
queue_delete_entry(void)
{
	struct track *t;

	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	if ((t = menu_get_selected_entry_data(queue_menu)) != NULL) {
		queue_duration -= t->duration;
		menu_remove_selected_entry(queue_menu);
	}
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

void
queue_end(void)
{
	menu_free(queue_menu);
}

static void
queue_free_entry(void *e)
{
	track_free((struct track *)e);
}

static void
queue_get_entry_text(const void *entry, char *buf, size_t bufsize)
{
	(void)track_snprintf(buf, bufsize, queue_print_format,
	    (const struct track *)entry);
}

struct track *
queue_get_next_track(void)
{
	struct menu_entry	*me;
	struct track		*t;

	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	if ((me = menu_get_first_entry(queue_menu)) == NULL)
		t = NULL;
	else {
		t = menu_get_entry_data(me);
		track_hold(t);
		queue_duration -= t->duration;
		menu_remove_first_entry(queue_menu);
	}
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);

	if (t != NULL)
		queue_print();

	return t;
}

void
queue_init(void)
{
	queue_menu = menu_init(queue_free_entry, queue_get_entry_text,
	    queue_search_entry);
}

void
queue_move_entry_down(void)
{
	struct menu_entry *e;

	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	if ((e = menu_get_selected_entry(queue_menu)) != NULL) {
		menu_move_entry_down(e);
		menu_select_next_entry(queue_menu);
	}
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

void
queue_move_entry_up(void)
{
	struct menu_entry *e;

	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	if ((e = menu_get_selected_entry(queue_menu)) != NULL) {
		menu_move_entry_up(e);
		menu_select_prev_entry(queue_menu);
	}
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

void
queue_print(void)
{
	if (view_get_id() != VIEW_ID_QUEUE)
		return;

	queue_print_format = option_get_string("queue-format");

	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	screen_view_title_printf("Queue: %u track%s (%u:%02u:%02u)",
	    menu_get_nentries(queue_menu),
	    menu_get_nentries(queue_menu) == 1 ? "" : "s",
	    HOURS(queue_duration),
	    HMINS(queue_duration),
	    MSECS(queue_duration));
	menu_print(queue_menu);
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);

	free(queue_print_format);
}

void
queue_scroll_down(enum menu_scroll scroll)
{
	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	menu_scroll_down(queue_menu, scroll);
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

void
queue_scroll_up(enum menu_scroll scroll)
{
	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	menu_scroll_up(queue_menu, scroll);
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

void
queue_search_next(const char *search)
{
	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	menu_search_next(queue_menu, search);
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

void
queue_search_prev(const char *search)
{
	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	menu_search_prev(queue_menu, search);
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

static int
queue_search_entry(const void *e, const char *search)
{
	const struct track *t;

	t = e;
	return track_search(t, search);
}

void
queue_select_first_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	menu_select_first_entry(queue_menu);
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

void
queue_select_last_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	menu_select_last_entry(queue_menu);
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

void
queue_select_next_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	menu_select_next_entry(queue_menu);
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}

void
queue_select_prev_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&queue_menu_mtx);
	menu_select_prev_entry(queue_menu);
	XPTHREAD_MUTEX_UNLOCK(&queue_menu_mtx);
	queue_print();
}
