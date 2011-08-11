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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* for access(2) */

#include "siren.h"

static void		 library_free_entry(void *);
static void		 library_get_entry_text(const void *, char *, size_t);
static int		 library_search_entry(const void *, const char *);

static pthread_mutex_t	 library_menu_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct menu	*library_menu;
static unsigned int	 library_duration;
static char		*library_print_format;

void
library_activate_entry(void)
{
	struct menu_entry	*e;
	struct track		*t;

	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	if ((e = menu_get_selected_entry(library_menu)) != NULL) {
		menu_activate_entry(library_menu, e);
		t = menu_get_entry_data(e);
		track_hold(t);
		player_play_track(t);
	}
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_add_track(struct track *t)
{
	struct track		*et;
	struct menu_entry	*entry;

	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	if (menu_is_empty(library_menu))
		menu_insert_head(library_menu, t);
	else {
		MENU_FOR_EACH_ENTRY(library_menu, entry) {
			et = menu_get_entry_data(entry);
			if (track_cmp(t, et) < 0) {
				menu_insert_before(library_menu, entry, t);
				break;
			}
		}

		if (entry == NULL)
			menu_insert_tail(library_menu, t);
	}

	library_duration += t->duration;
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_copy_entry(enum view_id view)
{
	struct track *t;

	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	if ((t = menu_get_selected_entry_data(library_menu)) != NULL)
		view_copy_track(VIEW_ID_LIBRARY, view, t);
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
}

void
library_delete_all_entries(void)
{
	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	menu_clear(library_menu);
	library_duration = 0;
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_delete_entry(void)
{
	struct menu_entry	*e;
	struct track		*t;

	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	if ((e = menu_get_selected_entry(library_menu)) != NULL) {
		t = menu_get_entry_data(e);
		library_duration -= t->duration;
		menu_remove_selected_entry(library_menu);
	}
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_end(void)
{
	(void)library_write_file();
	menu_free(library_menu);
}

static void
library_free_entry(void *e)
{
	struct track *t;

	t = e;
	track_free(t);
}

static void
library_get_entry_text(const void *e, char *buf, size_t bufsize)
{
	const struct track *t;

	t = e;
	(void)track_snprintf(buf, bufsize, library_print_format, t);
}

struct track *
library_get_next_track(void)
{
	struct menu_entry	*me;
	struct track		*t;

	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	if ((me = menu_get_active_entry(library_menu)) == NULL)
		t = NULL;
	else {
		if ((me = menu_get_next_entry(me)) == NULL)
			t = NULL;
		else {
			menu_activate_entry(library_menu, me);
			t = menu_get_entry_data(me);
			track_hold(t);
		}
	}
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
	return t;
}

struct track *
library_get_prev_track(void)
{
	struct menu_entry	*me;
	struct track		*t;

	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	if ((me = menu_get_active_entry(library_menu)) == NULL)
		t = NULL;
	else {
		if ((me = menu_get_prev_entry(me)) == NULL)
			t = NULL;
		else {
			menu_activate_entry(library_menu, me);
			t = menu_get_entry_data(me);
			track_hold(t);
		}
	}
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
	return t;
}

void
library_init(void)
{
	library_menu = menu_init(library_free_entry, library_get_entry_text,
	    library_search_entry);
}

void
library_print(void)
{
	if (view_get_id() != VIEW_ID_LIBRARY)
		return;

	library_print_format = option_get_string("library-format");

	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	screen_view_title_printf("Library: %u track%s (%u:%02u:%02u)",
	    menu_get_nentries(library_menu),
	    menu_get_nentries(library_menu) == 1 ? "" : "s",
	    HOURS(library_duration),
	    HMINS(library_duration),
	    MSECS(library_duration));
	menu_print(library_menu);
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);

	free(library_print_format);
}

void
library_read_file(void)
{
	struct track	*t;
	FILE		*fp;
	size_t		 len, lineno;
	char		*buf, *file, *lbuf;

	file = conf_path(LIBRARY_FILE);
	if ((fp = fopen(file, "r")) == NULL) {
		if (errno != ENOENT) {
			LOG_ERR("fopen: %s", file);
			msg_err("Cannot read library file");
		}
		free(file);
		return;
	}

	lbuf = NULL;
	for (lineno = 1; (buf = fgetln(fp, &len)); lineno++) {
		if (buf[len - 1] != '\n') {
			lbuf = xmalloc(len + 1);
			buf = memcpy(lbuf, buf, len++);
		}
		buf[len - 1] = '\0';

		if (buf[0] != '/') {
			LOG_ERRX("%s:%zu: invalid entry", file, lineno);
			continue;
		}

		if (access(buf, F_OK)) {
			LOG_ERR("access: %s", buf);
			continue;
		}

		if ((t = track_init(buf, NULL)) != NULL)
			library_add_track(t);
	}
	if (ferror(fp)) {
		LOG_ERR("fgetln: %s", buf);
		msg_err("Cannot read library");
	}
	free(lbuf);
	free(file);

	(void)fclose(fp);
}

void
library_scroll_down(enum menu_scroll scroll)
{
	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	menu_scroll_down(library_menu, scroll);
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_scroll_up(enum menu_scroll scroll)
{
	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	menu_scroll_up(library_menu, scroll);
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_search_next(const char *search)
{
	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	menu_search_next(library_menu, search);
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_search_prev(const char *search)
{
	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	menu_search_prev(library_menu, search);
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

static int
library_search_entry(const void *e, const char *search)
{
	const struct track *t;

	t = e;
	return track_search(t, search);
}

void
library_select_active_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	menu_select_active_entry(library_menu);
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_select_first_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	menu_select_first_entry(library_menu);
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_select_last_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	menu_select_last_entry(library_menu);
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_select_next_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	menu_select_next_entry(library_menu);
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_select_prev_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	menu_select_prev_entry(library_menu);
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

int
library_write_file(void)
{
	struct menu_entry	*entry;
	struct track		*t;
	FILE			*fp;
	int			 ret;
	char			*file;

	file = conf_path(LIBRARY_FILE);
	if ((fp = fopen(file, "w")) == NULL) {
		LOG_ERR("fopen: %s", file);
		msg_err("Cannot save library");
		ret = -1;
	} else {
		XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
		MENU_FOR_EACH_ENTRY(library_menu, entry) {
			t = menu_get_entry_data(entry);
			(void)fprintf(fp, "%s\n", t->path);
		}
		XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);

		(void)fclose(fp);
		ret = 0;
	}

	free(file);
	return ret;
}
