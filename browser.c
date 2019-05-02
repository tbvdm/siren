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

/* Let glibc expose strcasestr(). */
#define _GNU_SOURCE

#ifdef __OpenBSD__
#include <sys/queue.h>
#else
#include "compat/queue.h"
#endif

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "siren.h"

struct browser_entry {
	char		*name;
	enum file_type	 type;
	const struct ip	*ip;
};

static void		 browser_free_entry(void *);
static void		 browser_read_dir(void);
static int		 browser_search_entry(const void *, const char *);
static void		 browser_select_entry(const char *);

static pthread_mutex_t	 browser_menu_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct menu	*browser_menu;
static char		*browser_dir;

void
browser_activate_entry(void)
{
	struct menu_entry	*me;
	struct browser_entry	*be;
	struct track		*t;
	char			*path;

	if ((me = menu_get_selected_entry(browser_menu)) == NULL)
		return;

	be = menu_get_entry_data(me);
	switch (be->type) {
	case FILE_TYPE_DIRECTORY:
		browser_change_dir(be->name);
		break;
	case FILE_TYPE_REGULAR:
		xasprintf(&path, "%s/%s", browser_dir, be->name);
		if ((t = track_get(path, be->ip)) != NULL) {
			XPTHREAD_MUTEX_LOCK(&browser_menu_mtx);
			menu_activate_entry(browser_menu, me);
			XPTHREAD_MUTEX_UNLOCK(&browser_menu_mtx);
			player_set_source(PLAYER_SOURCE_BROWSER);
			player_play_track(t);
			browser_print();
		}
		free(path);
		break;
	default:
		msg_errx("Unsupported file type");
		break;
	}
}

void
browser_change_dir(const char *dir)
{
	char *newdir, *prevdir, *tmp;

	if (dir[0] == '/')
		newdir = path_normalise(dir);
	else {
		xasprintf(&tmp, "%s/%s", browser_dir, dir);
		newdir = path_normalise(tmp);
		free(tmp);
	}

	if (chdir(newdir) != 0) {
		msg_err("Cannot change to directory: %s", newdir);
		free(newdir);
		return;
	}

	/*
	 * If we are changing to the parent directory, remember the
	 * subdirectory we were in previously so that we can preselect it.
	 */
	prevdir = NULL;
	if (!strcmp(dir, "..")) {
		tmp = strrchr(browser_dir, '/');
		if (tmp != NULL && *++tmp != '\0')
			prevdir = xstrdup(tmp);
	}

	XPTHREAD_MUTEX_LOCK(&browser_menu_mtx);
	free(browser_dir);
	browser_dir = newdir;
	browser_read_dir();
	XPTHREAD_MUTEX_UNLOCK(&browser_menu_mtx);

	/* Preselect the subdirectory we were in previously, if applicable. */
	if (prevdir != NULL) {
		browser_select_entry(prevdir);
		free(prevdir);
	}

	browser_print();
}

void
browser_copy_entry(enum view_id view)
{
	struct browser_entry	*e;
	struct track		*t;
	char			*path, *tmp;

	if ((e = menu_get_selected_entry_data(browser_menu)) == NULL)
		return;

	xasprintf(&tmp, "%s/%s", browser_dir, e->name);
	path = path_normalise(tmp);
	free(tmp);

	switch (e->type) {
	case FILE_TYPE_REGULAR:
		if ((t = track_get(path, e->ip)) != NULL)
			view_add_track(view, t);
		break;
	case FILE_TYPE_DIRECTORY:
		view_add_dir(view, path);
		break;
	default:
		msg_errx("Unsupported file type");
		break;
	}

	free(path);
}

void
browser_end(void)
{
	menu_free(browser_menu);
	free(browser_dir);
}

const char *
browser_get_dir(void)
{
	return browser_dir;
}

static void
browser_free_entry(void *e)
{
	struct browser_entry *be;

	be = e;
	free(be->name);
	free(be);
}

static void
browser_get_entry_text(const void *e, char *buf, size_t bufsize)
{
	const struct browser_entry *be;

	be = e;
	strlcpy(buf, be->name, bufsize);
	if (be->type == FILE_TYPE_DIRECTORY)
		strlcat(buf, "/", bufsize);
}

struct track *
browser_get_next_track(void)
{
	struct menu_entry	*me;
	struct browser_entry	*be;
	struct track		*t;
	char			*path;

	XPTHREAD_MUTEX_LOCK(&browser_menu_mtx);
	if ((me = menu_get_active_entry(browser_menu)) == NULL)
		t = NULL;
	else
		for (;;) {
			if ((me = menu_get_next_entry(me)) == NULL) {
				if (!option_get_boolean("repeat-all")) {
					t = NULL;
					break;
				}
				me = menu_get_first_entry(browser_menu);
			}

			be = menu_get_entry_data(me);
			if (be->ip != NULL) {
				xasprintf(&path, "%s/%s", browser_dir,
				    be->name);
				t = track_get(path, be->ip);
				free(path);
				if (t != NULL)
					menu_activate_entry(browser_menu, me);
				break;
			}
		}
	XPTHREAD_MUTEX_UNLOCK(&browser_menu_mtx);

	browser_print();
	return t;
}

struct track *
browser_get_prev_track(void)
{
	struct menu_entry	*me;
	struct browser_entry	*be;
	struct track		*t;
	char			*path;

	XPTHREAD_MUTEX_LOCK(&browser_menu_mtx);
	if ((me = menu_get_active_entry(browser_menu)) == NULL)
		t = NULL;
	else
		for (;;) {
			if ((me = menu_get_prev_entry(me)) == NULL) {
				if (!option_get_boolean("repeat-all")) {
					t = NULL;
					break;
				}
				me = menu_get_last_entry(browser_menu);
			}

			be = menu_get_entry_data(me);
			if (be->ip != NULL) {
				xasprintf(&path, "%s/%s", browser_dir,
				    be->name);
				t = track_get(path, be->ip);
				free(path);
				if (t != NULL)
					menu_activate_entry(browser_menu, me);
				break;
			}
		}
	XPTHREAD_MUTEX_UNLOCK(&browser_menu_mtx);

	browser_print();
	return t;
}

void
browser_init(void)
{
	browser_menu = menu_init(browser_free_entry, browser_get_entry_text,
	    browser_search_entry);
	browser_dir = path_get_cwd();
	browser_read_dir();
}

void
browser_print(void)
{
	if (view_get_id() == VIEW_ID_BROWSER) {
		screen_view_title_printf("Browser: %s", browser_dir);
		menu_print(browser_menu);
	}
}

void
browser_reactivate_entry(void)
{
	struct menu_entry	*me;
	struct browser_entry	*be;
	struct track		*t;
	char			*path;

	XPTHREAD_MUTEX_LOCK(&browser_menu_mtx);
	if ((me = menu_get_active_entry(browser_menu)) != NULL) {
		be = menu_get_entry_data(me);
		xasprintf(&path, "%s/%s", browser_dir, be->name);
		if ((t = track_get(path, be->ip)) != NULL) {
			player_set_source(PLAYER_SOURCE_BROWSER);
			player_play_track(t);
		}
		free(path);
	}
	XPTHREAD_MUTEX_UNLOCK(&browser_menu_mtx);
}

/*
 * The browser_menu_mtx mutex must be locked before calling this function.
 */
static void
browser_read_dir(void)
{
	struct dir		*d;
	struct dir_entry	*de;
	struct menu_entry	*me;
	struct browser_entry	*be, *mbe;
	const struct ip		*ip;

	menu_remove_all_entries(browser_menu);

	if ((d = dir_open(browser_dir)) == NULL) {
		msg_err("Cannot open directory: %s", browser_dir);
		return;
	}

	while ((de = dir_get_entry(d)) != NULL) {
		if (de->type == FILE_TYPE_OTHER &&
		    !option_get_boolean("show-all-files"))
			continue;

		if (de->name[0] == '.') {
			if (de->name[1] == '\0')
				continue;

			if ((de->name[1] != '.' || de->name[2] != '\0') &&
			    !option_get_boolean("show-hidden-files"))
				continue;
		}

		if (de->type == FILE_TYPE_DIRECTORY)
			ip = NULL;
		else if ((ip = plugin_find_ip(de->path)) == NULL &&
		    !option_get_boolean("show-all-files"))
			continue;

		be = xmalloc(sizeof *be);
		be->name = xstrdup(de->name);
		be->type = de->type;
		be->ip = ip;

		MENU_FOR_EACH_ENTRY(browser_menu, me) {
			mbe = menu_get_entry_data(me);
			if (strcmp(be->name, mbe->name) < 0) {
				menu_insert_before(browser_menu, me, be);
				break;
			}
		}

		if (me == NULL)
			/*
			 * We have reached the end of the menu, so insert the
			 * new entry there.
			 */
			menu_insert_tail(browser_menu, be);
	}

	dir_close(d);
}

void
browser_refresh_dir(void)
{
	struct browser_entry	*e;
	char			*name;

	/* Remember the selected entry so that we can preselect it. */
	if ((e = menu_get_selected_entry_data(browser_menu)) == NULL)
		name = NULL;
	else
		name = xstrdup(e->name);

	XPTHREAD_MUTEX_LOCK(&browser_menu_mtx);
	browser_read_dir();
	XPTHREAD_MUTEX_UNLOCK(&browser_menu_mtx);

	/* Preselect the entry that was selected previously. */
	if (name != NULL) {
		browser_select_entry(name);
		free(name);
	}

	browser_print();
}

static int
browser_search_entry(const void *e, const char *search)
{
	const struct browser_entry *be;

	be = e;
	return strcasestr(be->name, search) != NULL ? 0 : -1;
}

void
browser_search_next(const char *search)
{
	menu_search_next(browser_menu, search);
	browser_print();
}

void
browser_search_prev(const char *search)
{
	menu_search_prev(browser_menu, search);
	browser_print();
}

void
browser_scroll_down(enum menu_scroll scroll)
{
	menu_scroll_down(browser_menu, scroll);
	browser_print();
}

void
browser_scroll_up(enum menu_scroll scroll)
{
	menu_scroll_up(browser_menu, scroll);
	browser_print();
}

void
browser_select_active_entry(void)
{
	menu_select_active_entry(browser_menu);
	browser_print();
}

static void
browser_select_entry(const char *name)
{
	struct menu_entry	*me;
	struct browser_entry	*be;

	MENU_FOR_EACH_ENTRY(browser_menu, me) {
		be = menu_get_entry_data(me);
		if (!strcmp(be->name, name)) {
			menu_select_entry(browser_menu, me);
			break;
		}
	}
}

void
browser_select_first_entry(void)
{
	menu_select_first_entry(browser_menu);
	browser_print();
}

void
browser_select_last_entry(void)
{
	menu_select_last_entry(browser_menu);
	browser_print();
}

void
browser_select_next_entry(void)
{
	menu_select_next_entry(browser_menu);
	browser_print();
}

void
browser_select_prev_entry(void)
{
	menu_select_prev_entry(browser_menu);
	browser_print();
}
