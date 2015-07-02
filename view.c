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

#include <stdlib.h>

#include "siren.h"

struct view_entry {
	enum view_id	 id;
	enum bind_scope	 bind_scope;
	void		 (*print)(void);
	void		 (*activate_entry)(void);
	void		 (*copy_entry)(enum view_id);
	void		 (*delete_all_entries)(void);
	void		 (*delete_entry)(void);
	void		 (*move_entry_down)(void);
	void		 (*move_entry_up)(void);
	void		 (*search_next)(const char *);
	void		 (*search_prev)(const char *);
	void		 (*select_active_entry)(void);
	void		 (*select_prev_entry)(void);
	void		 (*select_next_entry)(void);
	void		 (*select_first_entry)(void);
	void		 (*select_last_entry)(void);
	void		 (*scroll_down)(enum menu_scroll);
	void		 (*scroll_up)(enum menu_scroll);
};

struct view_entry	view_list[] = {
	{
		VIEW_ID_LIBRARY,
		BIND_SCOPE_LIBRARY,
		library_print,
		library_activate_entry,
		library_copy_entry,
		library_delete_all_entries,
		library_delete_entry,
		NULL,
		NULL,
		library_search_next,
		library_search_prev,
		library_select_active_entry,
		library_select_prev_entry,
		library_select_next_entry,
		library_select_first_entry,
		library_select_last_entry,
		library_scroll_down,
		library_scroll_up
	},
	{
		VIEW_ID_PLAYLIST,
		BIND_SCOPE_PLAYLIST,
		playlist_print,
		playlist_activate_entry,
		playlist_copy_entry,
		NULL,
		NULL,
		NULL,
		NULL,
		playlist_search_next,
		playlist_search_prev,
		playlist_select_active_entry,
		playlist_select_prev_entry,
		playlist_select_next_entry,
		playlist_select_first_entry,
		playlist_select_last_entry,
		playlist_scroll_down,
		playlist_scroll_up
	},
	{
		VIEW_ID_QUEUE,
		BIND_SCOPE_QUEUE,
		queue_print,
		queue_activate_entry,
		queue_copy_entry,
		queue_delete_all_entries,
		queue_delete_entry,
		queue_move_entry_down,
		queue_move_entry_up,
		queue_search_next,
		queue_search_prev,
		NULL,
		queue_select_prev_entry,
		queue_select_next_entry,
		queue_select_first_entry,
		queue_select_last_entry,
		queue_scroll_down,
		queue_scroll_up
	},
	{
		VIEW_ID_BROWSER,
		BIND_SCOPE_BROWSER,
		browser_print,
		browser_activate_entry,
		browser_copy_entry,
		NULL,
		NULL,
		NULL,
		NULL,
		browser_search_next,
		browser_search_prev,
		browser_select_active_entry,
		browser_select_prev_entry,
		browser_select_next_entry,
		browser_select_first_entry,
		browser_select_last_entry,
		browser_scroll_down,
		browser_scroll_up
	}
};

static int		 view_sel;
static char		*view_search = NULL;

void
view_activate_entry(void)
{
	view_list[view_sel].activate_entry();
}

void
view_add_dir(enum view_id view, const char *path)
{
	switch (view) {
	case VIEW_ID_LIBRARY:
		library_add_dir(path);
		break;
	case VIEW_ID_QUEUE:
		queue_add_dir(path);
		break;
	default:
		msg_errx("Cannot add tracks to this view");
		break;
	}
}

void
view_add_track(enum view_id view, struct track *t)
{
	switch (view) {
	case VIEW_ID_LIBRARY:
		library_add_track(t);
		break;
	case VIEW_ID_QUEUE:
		queue_add_track(t);
		break;
	default:
		msg_errx("Cannot add tracks to this view");
		break;
	}
}

void
view_copy_entry(enum view_id view)
{
	view_list[view_sel].copy_entry(view);
}

void
view_delete_all_entries(void)
{
	if (view_list[view_sel].delete_all_entries == NULL)
		msg_errx("Cannot delete entries in this view");
	else
		view_list[view_sel].delete_all_entries();
}

void
view_delete_entry(void)
{
	if (view_list[view_sel].delete_entry == NULL)
		msg_errx("Cannot delete entries in this view");
	else
		view_list[view_sel].delete_entry();
}

enum view_id
view_get_id(void)
{
	return view_list[view_sel].id;
}

void
view_handle_key(int key)
{
	msg_clear();

	if (bind_execute(view_list[view_sel].bind_scope, key) == 0)
		return;
	if (bind_execute(BIND_SCOPE_COMMON, key) == 0)
		return;

	msg_errx("Key not bound");
}

void
view_move_entry_down(void)
{
	if (view_list[view_sel].move_entry_down == NULL)
		msg_errx("Cannot move entries in this view");
	else
		view_list[view_sel].move_entry_down();
}

void
view_move_entry_up(void)
{
	if (view_list[view_sel].move_entry_up == NULL)
		msg_errx("Cannot move entries in this view");
	else
		view_list[view_sel].move_entry_up();
}

void
view_print(void)
{
	view_list[view_sel].print();
}

void
view_scroll_down(enum menu_scroll scroll)
{
	view_list[view_sel].scroll_down(scroll);
}

void
view_scroll_up(enum menu_scroll scroll)
{
	view_list[view_sel].scroll_up(scroll);
}

void
view_search_next(const char *search)
{
	if (search != NULL) {
		free(view_search);
		view_search = xstrdup(search);
	} else if (view_search == NULL) {
		msg_errx("No previous search");
		return;
	}

	view_list[view_sel].search_next(view_search);
}

void
view_search_prev(const char *search)
{
	if (search != NULL) {
		free(view_search);
		view_search = xstrdup(search);
	} else if (view_search == NULL) {
		msg_errx("No previous search");
		return;
	}

	view_list[view_sel].search_prev(view_search);
}

void
view_select_active_entry(void)
{
	if (view_list[view_sel].select_active_entry != NULL)
		view_list[view_sel].select_active_entry();
}

void
view_select_first_entry(void)
{
	view_list[view_sel].select_first_entry();
}

void
view_select_last_entry(void)
{
	view_list[view_sel].select_last_entry();
}

void
view_select_next_entry(void)
{
	view_list[view_sel].select_next_entry();
}

void
view_select_prev_entry(void)
{
	view_list[view_sel].select_prev_entry();
}

void
view_select_view(enum view_id id)
{
	size_t i;

	if (view_list[view_sel].id != id)
		for (i = 0; i < NELEMENTS(view_list); i++)
			if (view_list[i].id == id) {
				view_sel = i;
				view_print();
				break;
			}
}
