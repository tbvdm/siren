/*
 * Copyright (c) 2014 Tim van der Molen <tim@kariliq.nl>
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

/* For FreeBSD. */
#define _WITH_GETLINE

#include "config.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "siren.h"

static int		 playlist_search_entry(const void *, const char *);

static pthread_mutex_t	 playlist_menu_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct format	*playlist_altformat;
static struct format	*playlist_format;
static struct menu	*playlist_menu;
static unsigned int	 playlist_duration;
static char		*playlist_file;

void
playlist_activate_entry(void)
{
	struct menu_entry	*e;
	struct track		*t;

	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	if ((e = menu_get_selected_entry(playlist_menu)) == NULL)
		t = NULL;
	else {
		menu_activate_entry(playlist_menu, e);
		t = menu_get_entry_data(e);
	}
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);

	if (t != NULL) {
		player_set_source(PLAYER_SOURCE_PLAYLIST);
		player_play_track(t);
		playlist_print();
	}
}

void
playlist_copy_entry(enum view_id view)
{
	struct track *t;

	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	if ((t = menu_get_selected_entry_data(playlist_menu)) != NULL)
		view_add_track(view, t);
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
}

void
playlist_end(void)
{
	menu_free(playlist_menu);
	free(playlist_file);
}

static void
playlist_get_entry_text(const void *e, char *buf, size_t bufsize)
{
	const struct track *t;

	t = e;
	format_track_snprintf(buf, bufsize, playlist_format,
	    playlist_altformat, t);
}

struct track *
playlist_get_next_track(void)
{
	struct menu_entry	*e;
	struct track		*t;

	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	if ((e = menu_get_active_entry(playlist_menu)) == NULL)
		t = NULL;
	else {
		if ((e = menu_get_next_entry(e)) == NULL &&
		    option_get_boolean("repeat-all"))
			e = menu_get_first_entry(playlist_menu);

		if (e == NULL)
			t = NULL;
		else {
			menu_activate_entry(playlist_menu, e);
			t = menu_get_entry_data(e);
		}
	}
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);

	playlist_print();
	return t;
}

struct track *
playlist_get_prev_track(void)
{
	struct menu_entry	*e;
	struct track		*t;

	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	if ((e = menu_get_active_entry(playlist_menu)) == NULL)
		t = NULL;
	else {
		if ((e = menu_get_prev_entry(e)) == NULL &&
		    option_get_boolean("repeat-all"))
			e = menu_get_last_entry(playlist_menu);

		if (e == NULL)
			t = NULL;
		else {
			menu_activate_entry(playlist_menu, e);
			t = menu_get_entry_data(e);
		}
	}
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);

	playlist_print();
	return t;
}

void
playlist_init(void)
{
	playlist_menu = menu_init(NULL, playlist_get_entry_text,
	    playlist_search_entry);
}

void
playlist_load(const char *file)
{
	struct track		*t;
	FILE			*fp;
	size_t			 size;
	ssize_t			 len;
	char			*dir, *line, *path, *tmp;

	if ((fp = fopen(file, "r")) == NULL) {
		LOG_ERR("fopen: %s", file);
		msg_err("Cannot open playlist: %s", file);
		return;
	}

	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);

	menu_remove_all_entries(playlist_menu);
	free(playlist_file);
	playlist_duration = 0;

	playlist_file = path_normalise(file);
	dir = path_get_dirname(playlist_file);

	line = NULL;
	size = 0;
	while ((len = getline(&line, &size, fp)) != -1) {
		/* Strip both \n and \r\n EOLs. */
		if (len > 0 && line[len - 1] == '\n') {
			if (len > 1 && line[len - 2] == '\r')
				line[len - 2] = '\0';
			else
				line[len - 1] = '\0';
		}

		if (line[0] == '#' || line[0] == '\0')
			continue;

		if (line[0] == '/')
			path = path_normalise(line);
		else {
			xasprintf(&tmp, "%s/%s", dir, line);
			path = path_normalise(tmp);
			free(tmp);
		}

		if ((t = track_require(path)) != NULL) {
			menu_insert_tail(playlist_menu, t);
			playlist_duration += t->duration;
		}

		free(path);
	}

	if (ferror(fp)) {
		LOG_ERR("getline: %s", file);
		msg_err("Cannot read playlist: %s", file);
	}

	free(line);
	free(dir);

	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);

	fclose(fp);

	playlist_print();
}

void
playlist_reactivate_entry(void)
{
	struct menu_entry	*e;
	struct track		*t;

	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	if ((e = menu_get_active_entry(playlist_menu)) != NULL) {
		t = menu_get_entry_data(e);
		player_set_source(PLAYER_SOURCE_PLAYLIST);
		player_play_track(t);
	}
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
}

void
playlist_print(void)
{
	if (view_get_id() != VIEW_ID_PLAYLIST)
		return;

	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	screen_view_title_printf("Playlist: %s (%u track%s, %u:%02u:%02u)",
	    playlist_file != NULL ? playlist_file : "None",
	    menu_get_nentries(playlist_menu),
	    menu_get_nentries(playlist_menu) == 1 ? "" : "s",
	    HOURS(playlist_duration),
	    HMINS(playlist_duration),
	    MSECS(playlist_duration));
	option_lock();
	playlist_format = option_get_format("playlist-format");
	playlist_altformat = option_get_format("playlist-format-alt");
	menu_print(playlist_menu);
	option_unlock();
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
}

void
playlist_scroll_down(enum menu_scroll scroll)
{
	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	menu_scroll_down(playlist_menu, scroll);
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
	playlist_print();
}

void
playlist_scroll_up(enum menu_scroll scroll)
{
	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	menu_scroll_up(playlist_menu, scroll);
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
	playlist_print();
}

static int
playlist_search_entry(const void *e, const char *search)
{
	const struct track *t;

	t = e;
	return track_search(t, search);
}

void
playlist_search_next(const char *search)
{
	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	menu_search_next(playlist_menu, search);
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
	playlist_print();
}

void
playlist_search_prev(const char *search)
{
	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	menu_search_prev(playlist_menu, search);
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
	playlist_print();
}

void
playlist_select_active_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	menu_select_active_entry(playlist_menu);
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
	playlist_print();
}

void
playlist_select_first_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	menu_select_first_entry(playlist_menu);
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
	playlist_print();
}

void
playlist_select_last_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	menu_select_last_entry(playlist_menu);
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
	playlist_print();
}

void
playlist_select_next_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	menu_select_next_entry(playlist_menu);
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
	playlist_print();
}

void
playlist_select_prev_entry(void)
{
	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	menu_select_prev_entry(playlist_menu);
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
	playlist_print();
}

/* Recalculate the duration. */
void
playlist_update(void)
{
	struct menu_entry	*e;
	struct track		*t;

	XPTHREAD_MUTEX_LOCK(&playlist_menu_mtx);
	playlist_duration = 0;
	MENU_FOR_EACH_ENTRY(playlist_menu, e) {
		t = menu_get_entry_data(e);
		playlist_duration += t->duration;
	}
	XPTHREAD_MUTEX_UNLOCK(&playlist_menu_mtx);
}
