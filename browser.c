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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "siren.h"

#ifdef HAVE_QUEUE_H
#include <sys/queue.h>
#else
#include "compat/queue.h"
#endif

struct browser_entry {
	char		*name;
	enum file_type	 type;
	const struct ip	*ip;
};

static void		 browser_free_entry(void *);
static void		 browser_get_entry_text(const void *, char *, size_t);
static int		 browser_read_dir(const char *);
static int		 browser_search_entry(const void *, const char *);
static void		 browser_select_entry(const char *);

static struct menu	*browser_menu;
static char		*browser_dir;

void
browser_activate_entry(void)
{
	struct browser_entry	*e;
	struct track		*t;
	char			*path;

	if ((e = menu_get_selected_entry_data(browser_menu)) == NULL)
		return;

	switch (e->type) {
	case FILE_TYPE_DIRECTORY:
		browser_change_dir(e->name);
		break;
	case FILE_TYPE_REGULAR:
		(void)xasprintf(&path, "%s/%s", browser_dir, e->name);
		if ((t = track_init(path, e->ip)) != NULL)
			player_play_track(t);
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
	char *lastslash, *newdir, *prevdir, *tmp;

	prevdir = NULL;

	if (dir[0] == '/' || dir[0] == '~')
		newdir = path_normalise(dir);
	else {
		/*
		 * If we are going to the parent directory, remember the name
		 * of the current directory so that we can select it once we
		 * are in the parent directory.
		 */
		if (!strcmp(dir, "..") &&
		    (lastslash = strrchr(browser_dir, '/')) != NULL)
			prevdir = xstrdup(lastslash + 1);

		(void)xasprintf(&tmp, "%s/%s", browser_dir, dir);
		newdir = path_normalise(tmp);
		free(tmp);
	}

	if (browser_read_dir(newdir) == -1)
		free(newdir);
	else {
		free(browser_dir);
		browser_dir = newdir;

		if (prevdir != NULL)
			/* Select the remembered directory. */
			browser_select_entry(prevdir);

		browser_print();
	}

	free(prevdir);
}

void
browser_copy_entry(enum view_id view)
{
	struct browser_entry	*e;
	struct dir		*d;
	struct track		*t;
	int			 ret;
	char			*path, *tmp;

	if ((e = menu_get_selected_entry_data(browser_menu)) == NULL)
		return;

	(void)xasprintf(&tmp, "%s/%s", browser_dir, e->name);
	path = path_normalise(tmp);
	free(tmp);

	switch (e->type) {
	case FILE_TYPE_REGULAR:
		if ((t = track_init(path, e->ip)) != NULL)
			view_add_track(view, t);
		break;
	case FILE_TYPE_DIRECTORY:
		if ((d = dir_open(path)) == NULL)
			msg_err("Cannot open directory");
		else {
			while ((ret = dir_get_track(d, &t)) == 0 && t != NULL)
				view_add_track(view, t);

			if (ret)
				msg_err("Cannot read directory");

			dir_close(d);
		}
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
	(void)strlcpy(buf, be->name, bufsize);
	if (be->type == FILE_TYPE_DIRECTORY)
		(void)strlcat(buf, "/", bufsize);
}

void
browser_init(void)
{
	browser_menu = menu_init(browser_free_entry, browser_get_entry_text,
	    browser_search_entry);

	browser_dir = path_get_cwd();
	(void)browser_read_dir(browser_dir);
}

void
browser_print(void)
{
	if (view_get_id() == VIEW_ID_BROWSER) {
		screen_view_title_printf("Browser: %s", browser_dir);
		menu_print(browser_menu);
	}
}

static int
browser_read_dir(const char *dir)
{
	struct dir		*d;
	struct dir_entry	*de;
	struct menu_entry	*me;
	struct browser_entry	*be, *mbe;
	const struct ip		*ip;
	int			 ret;

	if ((d = dir_open(dir)) == NULL) {
		msg_err("Cannot open directory: %s", dir);
		return -1;
	}

	menu_remove_all_entries(browser_menu);

	while ((ret = dir_get_entry(d, &de)) == 0 && de != NULL) {
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

		if (de->type == FILE_TYPE_DIRECTORY ||
		    option_get_boolean("show-all-files"))
			ip = NULL;
		else if ((ip = plugin_find_ip(de->path)) == NULL)
			continue;

		be = xmalloc(sizeof *be);
		be->name = xstrdup(de->name);
		be->type = de->type;
		be->ip = ip;

		MENU_FOR_EACH_ENTRY(browser_menu, me) {
			mbe = menu_get_entry_data(me);
			if (option_get_boolean("show-dirs-before-files") &&
			    be->type != mbe->type) {
				if (be->type == FILE_TYPE_DIRECTORY) {
					menu_insert_before(browser_menu, me,
					    be);
					break;
				}
			} else if (strcmp(be->name, mbe->name) < 0) {
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

	if (ret)
		msg_err("Cannot read directory");

	dir_close(d);
	return 0;
}

void
browser_refresh_dir(void)
{
	struct browser_entry	*e;
	char			*name;

	if ((e = menu_get_selected_entry_data(browser_menu)) == NULL)
		name = NULL;
	else
		/* Remember selected entry. */
		name = xstrdup(e->name);

	if (browser_read_dir(browser_dir) != -1) {
		if (name != NULL) {
			/* Select remembered entry. */
			browser_select_entry(name);
			free(name);
		}
		browser_print();
	}
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
