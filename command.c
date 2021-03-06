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

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "siren.h"

struct command {
	const char	*name;
	int		 (*parse)(int, char **, void **, char **);
	void		 (*exec)(void *);
	void		 (*free)(void *);
};

struct command_add_path_data {
	int		  use_current_view;
	enum view_id	  view;
	char		**paths;
};

struct command_bind_key_data {
	enum bind_scope	  scope;
	int		  key;
	struct command	 *command;
	void		 *command_data;
	char		 *command_string;
};

struct command_seek_data {
	int		 position;
	int		 relative;
};

struct command_set_data {
	char		 *name;
	enum option_type  type;
	union {
		struct format	*format;
		int		 colour;
		int		 attrib;
		int		 boolean;
		int		 number;
		char		*string;
	}		  value;
};

struct command_set_volume_data {
	int		  volume;
	int		  relative;
};

struct command_show_binding_data {
	enum bind_scope	  scope;
	int		  key;
};

struct command_unbind_key_data {
	enum bind_scope	  scope;
	int		  key;
};

#define COMMAND_EXEC_PROTOTYPE(cmd) \
    static void command_ ## cmd ## _exec(void *)
#define COMMAND_FREE_PROTOTYPE(cmd) \
    static void command_ ## cmd ## _free(void *)
#define COMMAND_PARSE_PROTOTYPE(cmd) \
    static int command_ ## cmd ## _parse(int, char **, void **, char **)

COMMAND_EXEC_PROTOTYPE(activate_entry);
COMMAND_EXEC_PROTOTYPE(add_entry);
COMMAND_PARSE_PROTOTYPE(add_entry);
COMMAND_EXEC_PROTOTYPE(add_path);
COMMAND_FREE_PROTOTYPE(add_path);
COMMAND_PARSE_PROTOTYPE(add_path);
COMMAND_EXEC_PROTOTYPE(bind_key);
COMMAND_FREE_PROTOTYPE(bind_key);
COMMAND_PARSE_PROTOTYPE(bind_key);
COMMAND_EXEC_PROTOTYPE(cd);
COMMAND_PARSE_PROTOTYPE(cd);
COMMAND_EXEC_PROTOTYPE(close_output_plugin);
COMMAND_EXEC_PROTOTYPE(command_prompt);
COMMAND_EXEC_PROTOTYPE(delete_entry);
COMMAND_PARSE_PROTOTYPE(delete_entry);
COMMAND_PARSE_PROTOTYPE(generic);
COMMAND_EXEC_PROTOTYPE(load_playlist);
COMMAND_PARSE_PROTOTYPE(load_playlist);
COMMAND_EXEC_PROTOTYPE(move_entry_down);
COMMAND_EXEC_PROTOTYPE(move_entry_up);
COMMAND_EXEC_PROTOTYPE(pause);
COMMAND_EXEC_PROTOTYPE(play);
COMMAND_EXEC_PROTOTYPE(play_active);
COMMAND_EXEC_PROTOTYPE(play_next);
COMMAND_EXEC_PROTOTYPE(play_prev);
COMMAND_EXEC_PROTOTYPE(pwd);
COMMAND_EXEC_PROTOTYPE(quit);
COMMAND_EXEC_PROTOTYPE(refresh_directory);
COMMAND_EXEC_PROTOTYPE(refresh_screen);
COMMAND_EXEC_PROTOTYPE(reopen_output_plugin);
COMMAND_EXEC_PROTOTYPE(save_library);
COMMAND_EXEC_PROTOTYPE(save_metadata);
COMMAND_PARSE_PROTOTYPE(scroll);
COMMAND_EXEC_PROTOTYPE(scroll_down);
COMMAND_EXEC_PROTOTYPE(scroll_up);
COMMAND_EXEC_PROTOTYPE(search_next);
COMMAND_EXEC_PROTOTYPE(search_prev);
COMMAND_EXEC_PROTOTYPE(search_prompt);
COMMAND_PARSE_PROTOTYPE(search_prompt);
COMMAND_EXEC_PROTOTYPE(seek);
COMMAND_PARSE_PROTOTYPE(seek);
COMMAND_EXEC_PROTOTYPE(select_active_entry);
COMMAND_EXEC_PROTOTYPE(select_first_entry);
COMMAND_EXEC_PROTOTYPE(select_last_entry);
COMMAND_EXEC_PROTOTYPE(select_next_entry);
COMMAND_EXEC_PROTOTYPE(select_prev_entry);
COMMAND_EXEC_PROTOTYPE(select_view);
COMMAND_PARSE_PROTOTYPE(select_view);
COMMAND_EXEC_PROTOTYPE(set);
COMMAND_FREE_PROTOTYPE(set);
COMMAND_PARSE_PROTOTYPE(set);
COMMAND_EXEC_PROTOTYPE(set_playback_source);
COMMAND_PARSE_PROTOTYPE(set_playback_source);
COMMAND_EXEC_PROTOTYPE(set_volume);
COMMAND_PARSE_PROTOTYPE(set_volume);
COMMAND_EXEC_PROTOTYPE(show_binding);
COMMAND_PARSE_PROTOTYPE(show_binding);
COMMAND_EXEC_PROTOTYPE(show_option);
COMMAND_PARSE_PROTOTYPE(show_option);
COMMAND_EXEC_PROTOTYPE(source);
COMMAND_PARSE_PROTOTYPE(source);
COMMAND_EXEC_PROTOTYPE(stop);
COMMAND_EXEC_PROTOTYPE(unbind_key);
COMMAND_PARSE_PROTOTYPE(unbind_key);
COMMAND_EXEC_PROTOTYPE(update_metadata);
COMMAND_PARSE_PROTOTYPE(update_metadata);

static struct command command_list[] = {
	{
		"activate-entry",
		command_generic_parse,
		command_activate_entry_exec,
		NULL
	},
	{
		"add-entry",
		command_add_entry_parse,
		command_add_entry_exec,
		free
	},
	{
		"add-path",
		command_add_path_parse,
		command_add_path_exec,
		command_add_path_free
	},
	{
		"bind-key",
		command_bind_key_parse,
		command_bind_key_exec,
		command_bind_key_free
	},
	{
		"cd",
		command_cd_parse,
		command_cd_exec,
		free
	},
	{
		"close-output-plugin",
		command_generic_parse,
		command_close_output_plugin_exec,
		NULL
	},
	{
		"command-prompt",
		command_generic_parse,
		command_command_prompt_exec,
		NULL
	},
	{
		"delete-entry",
		command_delete_entry_parse,
		command_delete_entry_exec,
		free
	},
	{
		"load-playlist",
		command_load_playlist_parse,
		command_load_playlist_exec,
		free
	},
	{
		"move-entry-down",
		command_generic_parse,
		command_move_entry_down_exec,
		NULL
	},
	{
		"move-entry-up",
		command_generic_parse,
		command_move_entry_up_exec,
		NULL
	},
	{
		"pause",
		command_generic_parse,
		command_pause_exec,
		NULL
	},
	{
		"play",
		command_generic_parse,
		command_play_exec,
		NULL
	},
	{
		"play-active",
		command_generic_parse,
		command_play_active_exec,
		NULL
	},
	{
		"play-next",
		command_generic_parse,
		command_play_next_exec,
		NULL
	},
	{
		"play-prev",
		command_generic_parse,
		command_play_prev_exec,
		NULL
	},
	{
		"pwd",
		command_generic_parse,
		command_pwd_exec,
		NULL,
	},
	{
		"quit",
		command_generic_parse,
		command_quit_exec,
		NULL
	},
	{
		"refresh-screen",
		command_generic_parse,
		command_refresh_screen_exec,
		NULL
	},
	{
		"reread-directory",
		command_generic_parse,
		command_refresh_directory_exec,
		NULL
	},
	{
		"reopen-output-plugin",
		command_generic_parse,
		command_reopen_output_plugin_exec,
		NULL
	},
	{
		"save-library",
		command_generic_parse,
		command_save_library_exec,
		NULL
	},
	{
		"save-metadata",
		command_generic_parse,
		command_save_metadata_exec,
		NULL
	},
	{
		"scroll-down",
		command_scroll_parse,
		command_scroll_down_exec,
		free
	},
	{
		"scroll-up",
		command_scroll_parse,
		command_scroll_up_exec,
		free
	},
	{
		"search-next",
		command_generic_parse,
		command_search_next_exec,
		NULL
	},
	{
		"search-prev",
		command_generic_parse,
		command_search_prev_exec,
		NULL
	},
	{
		"search-prompt",
		command_search_prompt_parse,
		command_search_prompt_exec,
		free
	},
	{
		"seek",
		command_seek_parse,
		command_seek_exec,
		free
	},
	{
		"select-active-entry",
		command_generic_parse,
		command_select_active_entry_exec,
		NULL
	},
	{
		"select-first-entry",
		command_generic_parse,
		command_select_first_entry_exec,
		NULL,
	},
	{
		"select-last-entry",
		command_generic_parse,
		command_select_last_entry_exec,
		NULL
	},
	{
		"select-next-entry",
		command_generic_parse,
		command_select_next_entry_exec,
		NULL
	},
	{
		"select-prev-entry",
		command_generic_parse,
		command_select_prev_entry_exec,
		NULL,
	},
	{
		"select-view",
		command_select_view_parse,
		command_select_view_exec,
		free
	},
	{
		"set",
		command_set_parse,
		command_set_exec,
		command_set_free
	},
	{
		"set-playback-source",
		command_set_playback_source_parse,
		command_set_playback_source_exec,
		free
	},
	{
		"set-volume",
		command_set_volume_parse,
		command_set_volume_exec,
		free
	},
	{
		"show-binding",
		command_show_binding_parse,
		command_show_binding_exec,
		free
	},
	{
		"show-option",
		command_show_option_parse,
		command_show_option_exec,
		free
	},
	{
		"source",
		command_source_parse,
		command_source_exec,
		free
	},
	{
		"stop",
		command_generic_parse,
		command_stop_exec,
		NULL
	},
	{
		"unbind-key",
		command_unbind_key_parse,
		command_unbind_key_exec,
		free
	},
	{
		"update-metadata",
		command_update_metadata_parse,
		command_update_metadata_exec,
		free
	}
};

static void
command_activate_entry_exec(UNUSED void *datap)
{
	view_activate_entry();
}

static void
command_add_entry_exec(void *datap)
{
	view_copy_entry(*(enum view_id *)datap);
}

static int
command_add_entry_parse(int argc, char **argv, void **datap, char **error)
{
	enum view_id	*view;
	int		 c;

	view = xmalloc(sizeof *view);
	*view = VIEW_ID_LIBRARY;

	while ((c = getopt(argc, argv, "lq")) != -1)
		switch (c) {
		case 'l':
			*view = VIEW_ID_LIBRARY;
			break;
		case 'q':
			*view = VIEW_ID_QUEUE;
			break;
		default:
			goto usage;
		}

	if (argc != optind)
		goto usage;

	*datap = view;
	return 0;

usage:
	*error = xstrdup("Usage: add-entry [-l | -q]");
	free(view);
	return -1;
}

static void
command_add_path_exec(void *datap)
{
	struct command_add_path_data	*data;
	struct track			*t;
	struct stat			 sb;
	int				 i;

	data = datap;

	if (data->use_current_view)
		data->view = view_get_id();

	for (i = 0; data->paths[i] != NULL; i++) {
		if (stat(data->paths[i], &sb) == -1) {
			LOG_ERR("stat: %s", data->paths[i]);
			msg_err("%s", data->paths[i]);
			continue;
		}

		switch (sb.st_mode & S_IFMT) {
		case S_IFDIR:
			view_add_dir(data->view, data->paths[i]);
			break;
		case S_IFREG:
			if ((t = track_get(data->paths[i], NULL)) != NULL)
				view_add_track(data->view, t);
			break;
		default:
			msg_errx("%s: Unsupported file type", data->paths[i]);
			break;
		}
	}
}

static void
command_add_path_free(void *datap)
{
	struct command_add_path_data	*data;
	int				 i;

	data = datap;
	for (i = 0; data->paths[i] != NULL; i++)
		free(data->paths[i]);
	free(data->paths);
	free(data);
}

static int
command_add_path_parse(int argc, char **argv, void **datap, char **error)
{
	struct command_add_path_data	*data;
	int				 c, i;

	data = xmalloc(sizeof *data);
	data->use_current_view = 1;

	while ((c = getopt(argc, argv, "lq")) != -1)
		switch (c) {
		case 'l':
			data->view = VIEW_ID_LIBRARY;
			data->use_current_view = 0;
			break;
		case 'q':
			data->view = VIEW_ID_QUEUE;
			data->use_current_view = 0;
			break;
		default:
			goto usage;
		}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		goto usage;

	data->paths = xreallocarray(NULL, argc + 1, sizeof *data->paths);
	for (i = 0; i < argc; i++)
		data->paths[i] = path_normalise(argv[i]);
	data->paths[i] = NULL;

	*datap = data;
	return 0;

usage:
	*error = xstrdup("Usage: add-path [-l | -q] path ...");
	free(data);
	return -1;
}

static void
command_bind_key_exec(void *datap)
{
	struct command_bind_key_data *data;

	data = datap;
	bind_set(data->scope, data->key, data->command, data->command_data,
	    data->command_string);
}

static void
command_bind_key_free(void *datap)
{
	struct command_bind_key_data *data;

	data = datap;
	free(data->command_string);
	free(data);
}

static int
command_bind_key_parse(int argc, char **argv, void **datap, char **error)
{
	struct command_bind_key_data *data;

	if (argc != 4) {
		*error = xstrdup("Usage: bind-key scope key command");
		return -1;
	}

	data = xmalloc(sizeof *data);

	if (bind_string_to_scope(argv[1], &data->scope) == -1) {
		xasprintf(error, "Invalid scope: %s", argv[1]);
		free(data);
		return -1;
	}

	if ((data->key = bind_string_to_key(argv[2])) == K_NONE) {
		xasprintf(error, "Invalid key: %s", argv[2]);
		free(data);
		return -1;
	}

	if (command_parse_string(argv[3], &data->command, &data->command_data,
	    error)) {
		free(data);
		return -1;
	}

	if (data->command == NULL) {
		xasprintf(error, "Missing command: %s", argv[3]);
		free(data);
		return -1;
	}

	data->command_string = xstrdup(argv[3]);
	*datap = data;
	return 0;
}

static void
command_cd_exec(void *datap)
{
	char *dir;

	dir = datap;
	browser_change_dir(dir);
}

static int
command_cd_parse(int argc, char **argv, void **datap, char **error)
{
	if (argc > 2) {
		*error = xstrdup("Usage: cd [directory]");
		return -1;
	}

	if (argc == 2)
		*datap = xstrdup(argv[1]);
	else
		if ((*datap = path_get_home_dir(NULL)) == NULL) {
			*error = xstrdup("Cannot determine home directory");
			return -1;
		}

	return 0;
}

static void
command_close_output_plugin_exec(UNUSED void *datap)
{
	player_forcibly_close_op();
}

static void
command_command_prompt_callback(char *command, UNUSED void *datap)
{
	char *error;

	if (command != NULL) {
		if (command_process(command, &error)) {
			msg_errx("%s", error);
			free(error);
		}
		free(command);
	}
}

static void
command_command_prompt_exec(UNUSED void *datap)
{
	prompt_get_command(":", command_command_prompt_callback, NULL);
}

static void
command_delete_entry_callback(char *answer, void *datap)
{
	int *delete_all;

	if (*answer == 'y') {
		delete_all = datap;
		if (*delete_all)
			view_delete_all_entries();
		else
			view_delete_entry();
	}
	free(answer);
}

static void
command_delete_entry_exec(void *datap)
{
	int		*delete_all;
	const char	*prompt;

	delete_all = datap;
	prompt = *delete_all ? "Delete all entries" : "Delete entry";
	prompt_get_answer(prompt, command_delete_entry_callback, datap);
}

static int
command_delete_entry_parse(int argc, char **argv, void **datap, char **error)
{
	int c, *delete_all;

	delete_all = xmalloc(sizeof *delete_all);
	*delete_all = 0;

	while ((c = getopt(argc, argv, "a")) != -1)
		switch (c) {
		case 'a':
			*delete_all = 1;
			break;
		default:
			goto usage;
		}

	if (argc != optind)
		goto usage;

	*datap = delete_all;
	return 0;

usage:
	*error = xstrdup("Usage: delete-entry [-a]");
	free(delete_all);
	return -1;
}

void
command_execute(struct command *cmd, void *data)
{
	cmd->exec(data);
}

void
command_free_data(struct command *cmd, void *data)
{
	if (cmd->free != NULL)
		cmd->free(data);
}

static int
command_generic_parse(int argc, char **argv, void **datap, char **error)
{
	if (argc != 1) {
		xasprintf(error, "Usage: %s", argv[0]);
		return -1;
	}

	*datap = NULL;
	return 0;
}

static void
command_load_playlist_exec(void *datap)
{
	char *file;

	file = datap;
	playlist_load(file);
}

static int
command_load_playlist_parse(int argc, char **argv, void **datap, char **error)
{
	if (argc != 2) {
		*error = xstrdup("Usage: load-playlist file");
		return -1;
	}

	*datap = xstrdup(argv[1]);
	return 0;
}

static void
command_move_entry_down_exec(UNUSED void *datap)
{
	view_move_entry_down();
}

static void
command_move_entry_up_exec(UNUSED void *datap)
{
	view_move_entry_up();
}

int
command_parse_string(const char *str, struct command **cmd,
    void **cmd_data, char **error)
{
	size_t		  i;
	int		  argc, ret;
	char		**argv;

	if ((ret = argv_parse(str, &argc, &argv)) != 0) {
		*error = xstrdup(argv_error(ret));
		return -1;
	}

	*cmd = NULL;
	if (argc == 0)
		return 0;

	for (i = 0; i < nitems(command_list); i++)
		if (!strcmp(argv[0], command_list[i].name)) {
			*cmd = &command_list[i];
			break;
		}

	if (*cmd == NULL) {
		xasprintf(error, "No such command: %s", argv[0]);
		ret = -1;
	} else {
		optind = optreset = 1;
		ret = (*cmd)->parse(argc, argv, cmd_data, error);
	}

	argv_free(argc, argv);
	return ret;
}

static void
command_pause_exec(UNUSED void *datap)
{
	player_pause();
}

static void
command_play_exec(UNUSED void *datap)
{
	player_play();
}

static void
command_play_active_exec(UNUSED void *datap)
{
	view_reactivate_entry();
}

static void
command_play_next_exec(UNUSED void *datap)
{
	player_play_next();
}

static void
command_play_prev_exec(UNUSED void *datap)
{
	player_play_prev();
}

int
command_process(const char *line, char **error)
{
	struct command	 *cmd;
	void		 *cmd_data;

	if (command_parse_string(line, &cmd, &cmd_data, error))
		return -1;

	if (cmd != NULL) {
		command_execute(cmd, cmd_data);
		command_free_data(cmd, cmd_data);
	}
	return 0;
}

static
void
command_pwd_exec(UNUSED void *datap)
{
	msg_info("%s", browser_get_dir());
}

static void
command_quit_callback(char *answer, UNUSED void *datap)
{
	if (*answer == 'y')
		input_end();
	free(answer);
}

static void
command_quit_exec(UNUSED void *datap)
{
	prompt_get_answer("Quit", command_quit_callback, NULL);
}

static void
command_refresh_directory_exec(UNUSED void *datap)
{
	browser_refresh_dir();
}

static void
command_refresh_screen_exec(UNUSED void *datap)
{
	screen_refresh();
}

static void
command_reopen_output_plugin_exec(UNUSED void *datap)
{
	player_reopen_op();
}

static void
command_save_library_exec(UNUSED void *datap)
{
	if (library_write_file() == 0)
		msg_info("Library saved");
}

static void
command_save_metadata_exec(UNUSED void *datap)
{
	if (track_write_cache())
		msg_err("Cannot save metadata");
	else
		msg_info("Metadata saved");
}

static void
command_scroll_down_exec(void *datap)
{
	enum menu_scroll *scroll;

	scroll = datap;
	view_scroll_down(*scroll);
}

static int
command_scroll_parse(int argc, char **argv, void **datap, char **error)
{
	enum menu_scroll	*scroll;
	int			 c;

	scroll = xmalloc(sizeof *scroll);
	*scroll = MENU_SCROLL_LINE;

	while ((c = getopt(argc, argv, "hlp")) != -1)
		switch (c) {
		case 'h':
			*scroll = MENU_SCROLL_HALF_PAGE;
			break;
		case 'l':
			*scroll = MENU_SCROLL_LINE;
			break;
		case 'p':
			*scroll = MENU_SCROLL_PAGE;
			break;
		default:
			xasprintf(error, "Usage: %s [-h | -l | -p]", argv[0]);
			free(scroll);
			return -1;
		}

	*datap = scroll;
	return 0;
}

static void
command_scroll_up_exec(void *datap)
{
	enum menu_scroll *scroll;

	scroll = datap;
	view_scroll_up(*scroll);
}

static void
command_search_next_exec(UNUSED void *datap)
{
	view_search_next(NULL);
}

static void
command_search_prev_exec(UNUSED void *datap)
{
	view_search_prev(NULL);
}

static void
command_search_prompt_callback(char *query, void *datap)
{
	int *backward;

	if (query != NULL) {
		backward = datap;
		if (*backward)
			view_search_prev(query);
		else
			view_search_next(query);
		free(query);
	}
}

static void
command_search_prompt_exec(void *datap)
{
	int		*backward;
	const char	*prompt;

	backward = datap;
	prompt = *backward ? "?" : "/";
	prompt_get_search_query(prompt, command_search_prompt_callback, datap);
}

static int
command_search_prompt_parse(int argc, char **argv, void **datap, char **error)
{
	int *backward, c;

	backward = xmalloc(sizeof *backward);
	*backward = 0;

	while ((c = getopt(argc, argv, "b")) != -1)
		switch (c) {
		case 'b':
			*backward = 1;
			break;
		default:
			goto usage;
		}

	if (argc != optind)
		goto usage;

	*datap = backward;
	return 0;

usage:
	*error = xstrdup("Usage: search-prompt [-b]");
	free(backward);
	return -1;
}

static void
command_seek_exec(void *datap)
{
	struct command_seek_data *data;

	data = datap;
	player_seek(data->position, data->relative);
}

static int
command_seek_parse(int argc, char **argv, void **datap, char **error)
{
	struct command_seek_data	*data;
	int				 c;
	char				*field, *position;
	const char			*errstr;

	data = xmalloc(sizeof *data);
	data->relative = 0;

	while ((c = getopt(argc, argv, "bf")) != -1)
		switch (c) {
		case 'b':
			data->relative = -1;
			break;
		case 'f':
			data->relative = 1;
			break;
		default:
			goto usage;
		}

	if (argc != optind + 1)
		goto usage;

	/*
	 * The position argument is of the form "[[hours:]minutes:]seconds". It
	 * thus should have at least one colon-separated field and at most
	 * three.
	 */

	position = argv[optind];

	/* Parse the first field. */
	field = strsep(&position, ":");
	data->position = strtonum(field, 0, INT_MAX, &errstr);
	if (errstr != NULL)
		goto error;

	/* Parse the second field, if present. */
	if (position != NULL) {
		field = strsep(&position, ":");
		data->position *= 60;
		data->position += strtonum(field, 0, 59, &errstr);
		if (errstr != NULL)
			goto error;

		/* Parse the third field, if present. */
		if (position != NULL) {
			field = strsep(&position, ":");
			data->position *= 60;
			data->position += strtonum(field, 0, 59, &errstr);
			if (errstr != NULL)
				goto error;

			/* Ensure there is no fourth field. */
			if (position != NULL)
				goto error;
		}
	}

	if (data->relative)
		data->position *= data->relative;

	*datap = data;
	return 0;

usage:
	*error = xstrdup("Usage: seek [-bf] [[hours]:minutes:]seconds");
	free(data);
	return -1;

error:
	*error = xstrdup("Invalid position");
	free(data);
	return -1;
}

static void
command_select_active_entry_exec(UNUSED void *datap)
{
	view_select_active_entry();
}

static void
command_select_first_entry_exec(UNUSED void *datap)
{
	view_select_first_entry();
}

static void
command_select_last_entry_exec(UNUSED void *datap)
{
	view_select_last_entry();
}

static void
command_select_next_entry_exec(UNUSED void *datap)
{
	view_select_next_entry();
}

static void
command_select_prev_entry_exec(UNUSED void *datap)
{
	view_select_prev_entry();
}

static void
command_select_view_exec(void *datap)
{
	enum view_id *data;

	data = datap;
	view_select_view(*data);
}

static int
command_select_view_parse(int argc, char **argv, void **datap, char **error)
{
	enum view_id *data;

	if (argc != 2) {
		*error = xstrdup("Usage: select-view name");
		return -1;
	}

	data = xmalloc(sizeof *data);
	if (!strcmp(argv[1], "browser"))
		*data = VIEW_ID_BROWSER;
	else if (!strcmp(argv[1], "library"))
		*data = VIEW_ID_LIBRARY;
	else if (!strcmp(argv[1], "playlist"))
		*data = VIEW_ID_PLAYLIST;
	else if (!strcmp(argv[1], "queue"))
		*data = VIEW_ID_QUEUE;
	else {
		xasprintf(error, "Invalid view: %s", argv[1]);
		free(data);
		return -1;
	}

	*datap = data;
	return 0;
}

static void
command_set_exec(void *datap)
{
	struct command_set_data *data;

	data = datap;
	switch (data->type) {
	case OPTION_TYPE_ATTRIB:
		option_set_attrib(data->name, data->value.attrib);
		break;
	case OPTION_TYPE_COLOUR:
		option_set_colour(data->name, data->value.colour);
		break;
	case OPTION_TYPE_BOOLEAN:
		if (data->value.boolean == -1)
			option_toggle_boolean(data->name);
		else
			option_set_boolean(data->name, data->value.boolean);
		break;
	case OPTION_TYPE_FORMAT:
		option_set_format(data->name, data->value.format);
		break;
	case OPTION_TYPE_NUMBER:
		option_set_number(data->name, data->value.number);
		break;
	case OPTION_TYPE_STRING:
		option_set_string(data->name, data->value.string);
		break;
	}
}

static void
command_set_free(void *datap)
{
	struct command_set_data *data;

	data = datap;
	free(data->name);
	if (data->type == OPTION_TYPE_STRING)
		free(data->value.string);
	free(data);
}

static int
command_set_parse(int argc, char **argv, void **datap, char **error)
{
	struct command_set_data	*data;
	int			 attrib, max, min;
	char			*field, *fieldlist;
	const char		*errstr;

	if (argc < 2 || argc > 3) {
		*error = xstrdup("Usage: set option [value]");
		return -1;
	}

	data = xmalloc(sizeof *data);

	if (option_get_type(argv[1], &data->type) == -1) {
		xasprintf(error, "Invalid option: %s", argv[1]);
		goto error;
	}

	if (argc == 2 && data->type != OPTION_TYPE_BOOLEAN) {
		xasprintf(error, "Cannot toggle option: %s", argv[1]);
		goto error;
	}

	switch (data->type) {
	case OPTION_TYPE_ATTRIB:
		data->value.attrib = ATTRIB_NORMAL;
		attrib = -1;
		fieldlist = argv[2];

		while ((field = strsep(&fieldlist, ",")) != NULL) {
			if (*field == '\0')
				/* Empty field. */
				continue;
			if ((attrib = option_string_to_attrib(field)) == -1) {
				xasprintf(error, "Invalid attribute: %s",
				    field);
				goto error;
			}
			data->value.attrib |= attrib;
		}

		if (attrib == -1) {
			*error = xstrdup("Invalid attribute list");
			goto error;
		}
		break;
	case OPTION_TYPE_BOOLEAN:
		if (argc == 2)
			data->value.boolean = -1;
		else if ((data->value.boolean =
		    option_string_to_boolean(argv[2])) == -1) {
			xasprintf(error, "Invalid boolean: %s", argv[2]);
			goto error;
		}
		break;
	case OPTION_TYPE_COLOUR:
		if (option_string_to_colour(argv[2], &data->value.colour) ==
		    -1) {
			xasprintf(error, "Invalid colour: %s", argv[2]);
			goto error;
		}
		if (data->value.colour >= screen_get_ncolours()) {
			xasprintf(error, "Terminal does not support more than "
			    "%d colours ", screen_get_ncolours());
			goto error;
		}
		break;
	case OPTION_TYPE_FORMAT:
		data->value.format = format_parse(argv[2]);
		break;
	case OPTION_TYPE_NUMBER:
		option_get_number_range(argv[1], &min, &max);
		data->value.number = strtonum(argv[2], min, max, &errstr);
		if (errstr != NULL) {
			xasprintf(error, "Number is %s: %s", errstr, argv[2]);
			goto error;
		}
		break;
	case OPTION_TYPE_STRING:
		data->value.string = xstrdup(argv[2]);
		break;
	}

	data->name = xstrdup(argv[1]);
	*datap = data;
	return 0;

error:
	free(data);
	return -1;
}

static void
command_set_playback_source_exec(void *datap)
{
	enum player_source src;

	if (datap != NULL)
		src = *(enum player_source *)datap;
	else
		switch (view_get_id()) {
		case VIEW_ID_BROWSER:
			src = PLAYER_SOURCE_BROWSER;
			break;
		case VIEW_ID_LIBRARY:
			src = PLAYER_SOURCE_LIBRARY;
			break;
		case VIEW_ID_PLAYLIST:
			src = PLAYER_SOURCE_PLAYLIST;
			break;
		default:
			msg_errx("This view cannot be set as playback source");
			return;
		}

	player_set_source(src);
}

static int
command_set_playback_source_parse(int argc, char **argv, void **datap,
    char **error)
{
	enum player_source *src;

	if (argc > 2) {
		*error = xstrdup("Usage: set-playback-source [source]");
		return -1;
	}

	if (argc != 2)
		src = NULL;
	else {
		src = xmalloc(sizeof *src);
		if (!strcmp(argv[1], "browser"))
			*src = PLAYER_SOURCE_BROWSER;
		else if (!strcmp(argv[1], "library"))
			*src = PLAYER_SOURCE_LIBRARY;
		else if (!strcmp(argv[1], "playlist"))
			*src = PLAYER_SOURCE_PLAYLIST;
		else {
			xasprintf(error, "Invalid source: %s", argv[1]);
			free(src);
			return -1;
		}
	}

	*datap = src;
	return 0;
}

static void
command_set_volume_exec(void *datap)
{
	struct command_set_volume_data *data;

	data = datap;
	player_set_volume(data->volume, data->relative);
}

static int
command_set_volume_parse(int argc, char **argv, void **datap, char **error)
{
	struct command_set_volume_data	*data;
	int				 c;
	const char			*errstr;

	data = xmalloc(sizeof *data);
	data->relative = 0;

	while ((c = getopt(argc, argv, "di")) != -1)
		switch (c) {
		case 'd':
			data->relative = -1;
			break;
		case 'i':
			data->relative = 1;
			break;
		default:
			goto usage;
		}

	if (argc != optind + 1)
		goto usage;

	data->volume = strtonum(argv[optind], 0, 100, &errstr);
	if (errstr != NULL) {
		xasprintf(error, "Volume level is %s: %s", errstr,
		    argv[optind]);
		free(data);
		return -1;
	}

	if (data->relative)
		data->volume *= data->relative;

	*datap = data;
	return 0;

usage:
	*error = xstrdup("Usage: set-volume [-di] level");
	free(data);
	return -1;
}

static void
command_show_binding_exec(void *datap)
{
	struct command_show_binding_data	*data;
	const char				*command;

	data = datap;

	if ((command = bind_get_command(data->scope, data->key)) == NULL)
		msg_errx("No such key binding");
	else
		msg_info("%s", command);
}

static int
command_show_binding_parse(int argc, char **argv, void **datap, char **error)
{
	struct command_show_binding_data *data;

	if (argc != 3) {
		*error = xstrdup("Usage: show-binding context key");
		return -1;
	}

	data = xmalloc(sizeof *data);

	if (bind_string_to_scope(argv[1], &data->scope) == -1) {
		xasprintf(error, "Invalid scope: %s", argv[1]);
		free(data);
		return -1;
	}

	if ((data->key = bind_string_to_key(argv[2])) == K_NONE) {
		xasprintf(error, "Invalid key: %s", argv[2]);
		free(data);
		return -1;
	}

	*datap = data;
	return 0;
}

static void
command_show_option_exec(void *datap)
{
	enum option_type	 type;
	char			*name, *value;

	name = datap;
	if (option_get_type(name, &type) == -1) {
		msg_errx("Invalid option: %s", name);
		return;
	}

	switch (type) {
	case OPTION_TYPE_ATTRIB:
		value = option_attrib_to_string(option_get_attrib(name));
		msg_info("%s", value);
		free(value);
		break;
	case OPTION_TYPE_BOOLEAN:
		msg_info("%s",
		    option_boolean_to_string(option_get_boolean(name)));
		break;
	case OPTION_TYPE_COLOUR:
		value = option_colour_to_string(option_get_colour(name));
		msg_info("%s", value);
		free(value);
		break;
	case OPTION_TYPE_FORMAT:
		option_lock();
		msg_info("%s",
		    option_format_to_string(option_get_format(name)));
		option_unlock();
		break;
	case OPTION_TYPE_NUMBER:
		msg_info("%d", option_get_number(name));
		break;
	case OPTION_TYPE_STRING:
		value = option_get_string(name);
		msg_info("%s", value);
		free(value);
		break;
	}
}

static int
command_show_option_parse(int argc, char **argv, void **datap, char **error)
{
	if (argc != 2) {
		*error = xstrdup("Usage: show-option option");
		return -1;
	}

	*datap = xstrdup(argv[1]);
	return 0;
}

static void
command_source_exec(void *datap)
{
	conf_source_file(datap);
}

static int
command_source_parse(int argc, char **argv, void **datap, char **error)
{
	if (argc != 2) {
		*error = xstrdup("Usage: source file");
		return -1;
	}

	*datap = xstrdup(argv[1]);
	return 0;
}

static void
command_stop_exec(UNUSED void *datap)
{
	player_stop();
}

static void
command_unbind_key_exec(void *datap)
{
	struct command_unbind_key_data *data;

	data = datap;
	if (bind_unset(data->scope, data->key))
		msg_errx("No such key binding");
}

static int
command_unbind_key_parse(int argc, char **argv, void **datap, char **error)
{
	struct command_unbind_key_data *data;

	if (argc != 3) {
		*error = xstrdup("Usage: unbind-key scope key");
		return -1;
	}

	data = xmalloc(sizeof *data);

	if (bind_string_to_scope(argv[1], &data->scope) == -1) {
		xasprintf(error, "Invalid scope: %s", argv[1]);
		free(data);
		return -1;
	}

	if ((data->key = bind_string_to_key(argv[2])) == K_NONE) {
		xasprintf(error, "Invalid key: %s", argv[2]);
		free(data);
		return -1;
	}

	*datap = data;
	return 0;
}

static void
command_update_metadata_exec(void *datap)
{
	int delete;

	delete = *(int *)datap;
	track_update_metadata(delete);
	library_update();
	playlist_update();
	queue_update();
	screen_print();
}

static int
command_update_metadata_parse(int argc, char **argv, void **datap,
    char **error)
{
	int c, *delete;

	delete = xmalloc(sizeof *delete);
	*delete = 0;

	while ((c = getopt(argc, argv, "d")) != -1)
		switch (c) {
		case 'd':
			*delete = 1;
			break;
		default:
			goto usage;
		}

	if (argc != optind)
		goto usage;

	*datap = delete;
	return 0;

usage:
	*error = xstrdup("Usage: update-metadata [-d]");
	free(delete);
	return -1;
}
