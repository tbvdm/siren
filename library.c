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

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "siren.h"

static int		 library_search_entry(const void *, const char *);

static pthread_mutex_t	 library_menu_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct format	*library_altformat;
static struct format	*library_format;
static struct menu	*library_menu;
static unsigned int	 library_duration;
static int		 library_modified;

void
library_activate_entry(void)
{
	struct menu_entry	*e;
	struct track		*t;

	t = NULL;

	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	if ((e = menu_get_selected_entry(library_menu)) != NULL) {
		menu_activate_entry(library_menu, e);
		t = menu_get_entry_data(e);
	}
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);

	if (t != NULL) {
		player_set_source(PLAYER_SOURCE_LIBRARY);
		player_play_track(t);
		library_print();
	}
}

void
library_add_dir(const char *path)
{
	struct dir		*d;
	struct dir_entry	*de;
	struct track		*t;

	if ((d = dir_open(path)) == NULL) {
		msg_err("%s", path);
		return;
	}

	while ((de = dir_get_entry(d)) != NULL) {
		switch (de->type) {
		case FILE_TYPE_DIRECTORY:
			if (strcmp(de->name, ".") && strcmp(de->name, ".."))
				library_add_dir(de->path);
			break;
		case FILE_TYPE_REGULAR:
			if ((t = track_get(de->path, NULL)) != NULL)
				library_add_track(t);
			break;
		default:
			msg_errx("%s: Unsupported file type", de->path);
			break;
		}
	}

	dir_close(d);
}

void
library_add_track(struct track *t)
{
	struct track		*et;
	struct menu_entry	*entry;

	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	MENU_FOR_EACH_ENTRY(library_menu, entry) {
		et = menu_get_entry_data(entry);
		if (track_cmp(t, et) < 0) {
			menu_insert_before(library_menu, entry, t);
			break;
		}
	}

	if (entry == NULL)
		menu_insert_tail(library_menu, t);

	library_duration += t->duration;
	library_modified = 1;
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_copy_entry(enum view_id view)
{
	struct track *t;

	if (view == VIEW_ID_LIBRARY)
		return;

	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	if ((t = menu_get_selected_entry_data(library_menu)) != NULL)
		view_add_track(view, t);
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
}

void
library_delete_all_entries(void)
{
	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	menu_remove_all_entries(library_menu);
	library_duration = 0;
	library_modified = 1;
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
		menu_remove_selected_entry(library_menu);
		library_duration -= t->duration;
		library_modified = 1;
	}
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
}

void
library_end(void)
{
	if (library_modified)
		library_write_file();

	menu_free(library_menu);
}

static void
library_get_entry_text(const void *e, char *buf, size_t bufsize)
{
	const struct track *t;

	t = e;
	format_track_snprintf(buf, bufsize, library_format, library_altformat,
	    t);
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
		if ((me = menu_get_next_entry(me)) == NULL &&
		    option_get_boolean("repeat-all"))
			me = menu_get_first_entry(library_menu);

		if (me == NULL)
			t = NULL;
		else {
			menu_activate_entry(library_menu, me);
			t = menu_get_entry_data(me);
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
		if ((me = menu_get_prev_entry(me)) == NULL &&
		    option_get_boolean("repeat-all"))
			me = menu_get_last_entry(library_menu);

		if (me == NULL)
			t = NULL;
		else {
			menu_activate_entry(library_menu, me);
			t = menu_get_entry_data(me);
		}
	}
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
	library_print();
	return t;
}

void
library_init(void)
{
	library_menu = menu_init(NULL, library_get_entry_text,
	    library_search_entry);
}

void
library_print(void)
{
	if (view_get_id() != VIEW_ID_LIBRARY)
		return;

	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
	screen_view_title_printf("Library: %u track%s (%u:%02u:%02u)",
	    menu_get_nentries(library_menu),
	    menu_get_nentries(library_menu) == 1 ? "" : "s",
	    HOURS(library_duration),
	    HMINS(library_duration),
	    MSECS(library_duration));
	option_lock();
	library_format = option_get_format("library-format");
	library_altformat = option_get_format("library-format-alt");
	menu_print(library_menu);
	option_unlock();
	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
}

void
library_read_file(void)
{
	struct menu_entry	*e;
	struct track		*et, *t;
	FILE			*fp;
	size_t			 size;
	ssize_t			 len;
	time_t			 lasttime;
	char			*line, *file;

	file = conf_get_path(LIBRARY_FILE);
	if ((fp = fopen(file, "r")) == NULL) {
		if (errno != ENOENT) {
			LOG_ERR("fopen: %s", file);
			msg_err("Cannot read library file");
		}
		free(file);
		return;
	}

	lasttime = time(NULL);
	line = NULL;
	size = 0;
	while ((len = getline(&line, &size, fp)) != -1) {
		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';

		if (line[0] != '/') {
			LOG_ERRX("%s: %s: invalid entry", file, line);
			continue;
		}

		if ((t = track_require(line)) == NULL)
			continue;

		MENU_FOR_EACH_ENTRY_REVERSE(library_menu, e) {
			et = menu_get_entry_data(e);
			if (track_cmp(t, et) > 0) {
				menu_insert_after(library_menu, e, t);
				break;
			}
		}
		if (e == NULL)
			menu_insert_head(library_menu, t);
		library_duration += t->duration;

		if (time(NULL) > lasttime) {
			library_print();
			lasttime = time(NULL);
		}
	}
	if (ferror(fp)) {
		LOG_ERR("getline: %s", file);
		msg_err("Cannot read library");
	}
	free(line);
	free(file);

	fclose(fp);

	library_print();
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

static int
library_search_entry(const void *e, const char *search)
{
	const struct track *t;

	t = e;
	return track_search(t, search);
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

/* Resort the tracks and recalculate the duration. */
void
library_update(void)
{
	struct menu_entry	*be, *e, *ne, *pe;
	struct track		*pt, *t;

	XPTHREAD_MUTEX_LOCK(&library_menu_mtx);

	library_duration = 0;

	e = menu_get_first_entry(library_menu);
	while (e != NULL) {
		t = menu_get_entry_data(e);
		library_duration += t->duration;

		ne = menu_get_next_entry(e);

		be = NULL;
		pe = e;
		while ((pe = menu_get_prev_entry(pe)) != NULL) {
			pt = menu_get_entry_data(pe);
			if (track_cmp(t, pt) < 0)
				be = pe;
			else
				break;
		}
		if (be != NULL)
			menu_move_entry_before(library_menu, be, e);

		e = ne;
	}

	XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);
}

int
library_write_file(void)
{
	struct menu_entry	*entry;
	struct track		*t;
	FILE			*fp;
	int			 ret;
	char			*file;

	file = conf_get_path(LIBRARY_FILE);
	if ((fp = fopen(file, "w")) == NULL) {
		LOG_ERR("fopen: %s", file);
		msg_err("Cannot save library");
		ret = -1;
	} else {
		XPTHREAD_MUTEX_LOCK(&library_menu_mtx);
		MENU_FOR_EACH_ENTRY(library_menu, entry) {
			t = menu_get_entry_data(entry);
			fprintf(fp, "%s\n", t->path);
		}
		library_modified = 0;
		XPTHREAD_MUTEX_UNLOCK(&library_menu_mtx);

		fclose(fp);
		ret = 0;
	}

	free(file);
	return ret;
}
