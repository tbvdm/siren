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

#include <sys/types.h>
#include <sys/stat.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "siren.h"

#define COMMAND_CLEAR_HISTORY_ALL	~0
#define COMMAND_CLEAR_HISTORY_COMMAND	1
#define COMMAND_CLEAR_HISTORY_SEARCH	2

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

struct command_confirm_data {
	char		 *prompt;
	struct command	 *command;
	void		 *command_data;
};

struct command_search_prompt_data {
	int		  search_backward;
	char		 *prompt;
};

struct command_seek_data {
	int		 position;
	int		 relative;
};

struct command_set_data {
	char		 *name;
	enum option_type  type;
	union {
		enum colour	 colour;
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

struct command_unbind_key_data {
	enum bind_scope	  scope;
	int		  key;
};

static int command_string_to_argv(const char *, char ***, char **);

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
COMMAND_EXEC_PROTOTYPE(clear_history);
COMMAND_PARSE_PROTOTYPE(clear_history);
COMMAND_EXEC_PROTOTYPE(command_prompt);
COMMAND_PARSE_PROTOTYPE(command_prompt);
COMMAND_EXEC_PROTOTYPE(confirm);
COMMAND_FREE_PROTOTYPE(confirm);
COMMAND_PARSE_PROTOTYPE(confirm);
COMMAND_EXEC_PROTOTYPE(delete_entry);
COMMAND_PARSE_PROTOTYPE(delete_entry);
COMMAND_PARSE_PROTOTYPE(generic);
COMMAND_EXEC_PROTOTYPE(move_entry_down);
COMMAND_EXEC_PROTOTYPE(move_entry_up);
COMMAND_EXEC_PROTOTYPE(pause);
COMMAND_EXEC_PROTOTYPE(play);
COMMAND_EXEC_PROTOTYPE(play_next);
COMMAND_EXEC_PROTOTYPE(play_prev);
COMMAND_EXEC_PROTOTYPE(quit);
COMMAND_EXEC_PROTOTYPE(refresh_directory);
COMMAND_EXEC_PROTOTYPE(refresh_screen);
COMMAND_EXEC_PROTOTYPE(save_library);
COMMAND_PARSE_PROTOTYPE(scroll);
COMMAND_EXEC_PROTOTYPE(scroll_down);
COMMAND_EXEC_PROTOTYPE(scroll_up);
COMMAND_EXEC_PROTOTYPE(search_next);
COMMAND_EXEC_PROTOTYPE(search_prev);
COMMAND_EXEC_PROTOTYPE(search_prompt);
COMMAND_FREE_PROTOTYPE(search_prompt);
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
COMMAND_EXEC_PROTOTYPE(set_volume);
COMMAND_PARSE_PROTOTYPE(set_volume);
COMMAND_EXEC_PROTOTYPE(stop);
COMMAND_EXEC_PROTOTYPE(unbind_key);
COMMAND_PARSE_PROTOTYPE(unbind_key);

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
		"clear-history",
		command_clear_history_parse,
		command_clear_history_exec,
		free
	},
	{
		"command-prompt",
		command_command_prompt_parse,
		command_command_prompt_exec,
		free
	},
	{
		"confirm",
		command_confirm_parse,
		command_confirm_exec,
		command_confirm_free
	},
	{
		"delete-entry",
		command_delete_entry_parse,
		command_delete_entry_exec,
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
		"save-library",
		command_generic_parse,
		command_save_library_exec,
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
		command_search_prompt_free
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
		"set-volume",
		command_set_volume_parse,
		command_set_volume_exec,
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
	}
};

/* ARGSUSED */
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

	if (argc - optind != 0)
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
			if ((t = track_init(data->paths[i], NULL)) != NULL)
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

	data->paths = xcalloc(argc + 1, sizeof *data->paths);
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
		(void)xasprintf(error, "Invalid scope: %s", argv[1]);
		free(data);
		return -1;
	}

	if ((data->key = bind_string_to_key(argv[2])) == K_NONE) {
		(void)xasprintf(error, "Invalid key: %s", argv[2]);
		free(data);
		return -1;
	}

	if (command_parse_string(argv[3], &data->command, &data->command_data,
	    error)) {
		free(data);
		return -1;
	}

	if (data->command == NULL) {
		(void)xasprintf(error, "Missing command: %s", argv[3]);
		free(data);
		return -1;
	}

	data->command_string = xstrdup(argv[3]);
	*datap = data;
	return 0;
}

void
command_cd_exec(void *datap)
{
	char *dir;

	dir = datap;
	browser_change_dir(dir);
}

int
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
command_clear_history_exec(void *datap)
{
	int histories;

	histories = *(int *)datap;
	if (histories & COMMAND_CLEAR_HISTORY_COMMAND)
		prompt_clear_command_history();
	if (histories & COMMAND_CLEAR_HISTORY_SEARCH)
		prompt_clear_search_history();
}

static int
command_clear_history_parse(int argc, char **argv, void **datap, char **error)
{
	int c, *histories;

	histories = xmalloc(sizeof *histories);
	*histories = 0;

	while ((c = getopt(argc, argv, "cs")) != -1)
		switch (c) {
		case 'c':
			*histories |= COMMAND_CLEAR_HISTORY_COMMAND;
			break;
		case 's':
			*histories |= COMMAND_CLEAR_HISTORY_SEARCH;
			break;
		default:
			goto usage;
		}

	if (argc - optind != 0)
		goto usage;

	/* If no history is specified, then clear all histories. */
	if (*histories == 0)
		*histories = COMMAND_CLEAR_HISTORY_ALL;

	*datap = histories;
	return 0;

usage:
	*error = xstrdup("Usage: clear-history [-cs]");
	free(histories);
	return -1;
}

static void
command_command_prompt_exec(void *datap)
{
	char		*command, *error;
	const char	*prompt;

	prompt = datap != NULL ? (char *)datap : ":";
	if ((command = prompt_get_command(prompt)) == NULL)
		return;

	if (command_process(command, &error)) {
		msg_errx("%s", error);
		free(error);
	}

	free(command);
}

static int
command_command_prompt_parse(int argc, char **argv, void **datap, char **error)
{
	int	 c;
	char	*prompt;

	prompt = NULL;

	while ((c = getopt(argc, argv, "p:")) != -1)
		switch (c) {
		case 'p':
			free(prompt);
			prompt = xstrdup(optarg);
			break;
		default:
			goto usage;
		}

	if (argc - optind != 0)
		goto usage;

	*datap = prompt;
	return 0;

usage:
	*error = xstrdup("Usage: command-prompt [-p prompt]");
	free(prompt);
	return -1;
}

static void
command_confirm_exec(void *datap)
{
	struct command_confirm_data *data;

	data = datap;
	if (prompt_get_answer(data->prompt))
		command_execute(data->command, data->command_data);
}

static void
command_confirm_free(void *datap)
{
	struct command_confirm_data *data;

	data = datap;
	command_free_data(data->command, data->command_data);
	free(data->prompt);
	free(data);
}

static int
command_confirm_parse(int argc, char **argv, void **datap, char **error)
{
	struct command_confirm_data	*data;
	int				 c;

	data = xmalloc(sizeof *data);
	data->prompt = NULL;

	while ((c = getopt(argc, argv, "p:")) != -1)
		switch (c) {
		case 'p':
			free(data->prompt);
			data->prompt = xstrdup(optarg);
			break;
		default:
			goto usage;
		}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		goto usage;

	if (command_parse_string(argv[0], &data->command, &data->command_data,
	    error))
		goto error;

	if (data->command == NULL) {
		(void)xasprintf(error, "Missing command: %s", argv[0]);
		goto error;
	}

	if (data->prompt == NULL)
		(void)xasprintf(&data->prompt, "Execute \"%s\"", argv[0]);

	*datap = data;
	return 0;

usage:
	*error = xstrdup("Usage: confirm [-p prompt] command");

error:
	free(data->prompt);
	free(data);
	return -1;
}

static void
command_delete_entry_exec(void *datap)
{
	int delete_all;

	delete_all = *(int *)datap;
	if (delete_all) {
		view_delete_all_entries();
	} else
		view_delete_entry();
}

static int
command_delete_entry_parse(int argc, char **argv, void **datap, char **error)
{
	int	c, *delete_all;

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

	if (argc - optind != 0)
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

static void
command_free_argv(int argc, char **argv)
{
	if (argc) {
		while (argc-- > 0)
			free(argv[argc]);
		free(argv);
	}
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
		(void)xasprintf(error, "Usage: %s", argv[0]);
		return -1;
	}

	*datap = NULL;
	return 0;
}

/* ARGSUSED */
static void
command_move_entry_down_exec(UNUSED void *datap)
{
	view_move_entry_down();
}

/* ARGSUSED */
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

	if ((argc = command_string_to_argv(str, &argv, error)) == -1)
		return -1;

	*cmd = NULL;
	if (argc == 0)
		return 0;

	for (i = 0; i < NELEMENTS(command_list); i++)
		if (!strcmp(argv[0], command_list[i].name)) {
			*cmd = &command_list[i];
			break;
		}

	if (*cmd == NULL) {
		(void)xasprintf(error, "No such command: %s", argv[0]);
		ret = -1;
	} else {
		optind = optreset = 1;
		ret = (*cmd)->parse(argc, argv, cmd_data, error);
	}

	command_free_argv(argc, argv);
	return ret;
}

/* ARGSUSED */
static void
command_pause_exec(UNUSED void *datap)
{
	player_pause();
}

/* ARGSUSED */
static void
command_play_exec(UNUSED void *datap)
{
	player_play();
}

/* ARGSUSED */
static void
command_play_next_exec(UNUSED void *datap)
{
	player_play_next();
}

/* ARGSUSED */
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

/* ARGSUSED */
static void
command_quit_exec(UNUSED void *datap)
{
	extern int quit;

	quit = 1;
}

/* ARGSUSED */
static void
command_refresh_directory_exec(UNUSED void *datap)
{
	browser_refresh_dir();
}

/* ARGSUSED */
static void
command_refresh_screen_exec(UNUSED void *datap)
{
	screen_refresh();
}

/* ARGSUSED */
static void
command_save_library_exec(UNUSED void *datap)
{
	if (library_write_file() == 0)
		msg_info("Library saved");
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
			(void)xasprintf(error, "Usage: %s [-h | -l | -p]",
			    argv[0]);
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

/* ARGSUSED */
static void
command_search_next_exec(UNUSED void *datap)
{
	view_search_next(NULL);
}

/* ARGSUSED */
static void
command_search_prev_exec(UNUSED void *datap)
{
	view_search_prev(NULL);
}

static void
command_search_prompt_exec(void *datap)
{
	struct command_search_prompt_data *data;
	char *search;

	data = datap;
	if ((search = prompt_get_search(data->prompt)) != NULL) {
		if (data->search_backward)
			view_search_prev(search);
		else
			view_search_next(search);
	}

	free(search);
}

static void
command_search_prompt_free(void *datap)
{
	struct command_search_prompt_data *data;

	data = datap;
	free(data->prompt);
	free(data);
}

static int
command_search_prompt_parse(int argc, char **argv, void **datap, char **error)
{
	struct command_search_prompt_data *data;
	int c;

	data = xmalloc(sizeof *data);
	data->search_backward = 0;
	data->prompt = NULL;

	while ((c = getopt(argc, argv, "bp:")) != -1)
		switch (c) {
		case 'b':
			data->search_backward = 1;
			break;
		case 'p':
			free(data->prompt);
			data->prompt = xstrdup(optarg);
			break;
		default:
			goto usage;
		}

	if (argc - optind != 0)
		goto usage;

	if (data->prompt == NULL)
		data->prompt = xstrdup(data->search_backward ? "?" : "/");

	*datap = data;
	return 0;

usage:
	*error = xstrdup("Usage: search-prompt [-b] [-p prompt]");
	free(data->prompt);
	free(data);
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

	if (argc - optind != 1)
		goto usage;

	position = argv[optind];

	/* Parse the first field. */
	field = strsep(&position, ":");
	data->position = (int)strtonum(field, 0, INT_MAX, &errstr);
	if (errstr != NULL)
		goto error;

	/* Parse the second field, if present. */
	if ((field = strsep(&position, ":")) != NULL) {
		data->position *= 60;
		data->position += (int)strtonum(field, 0, 59, &errstr);
		if (errstr != NULL)
			goto error;

		/* Parse the third field, if present. */
		if ((field = strsep(&position, ":")) != NULL) {
			data->position *= 60;
			data->position += (int)strtonum(field, 0, 59, &errstr);
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

/* ARGSUSED */
static void
command_select_active_entry_exec(UNUSED void *datap)
{
	library_select_active_entry();
}

/* ARGSUSED */
static void
command_select_first_entry_exec(UNUSED void *datap)
{
	view_select_first_entry();
}

/* ARGSUSED */
static void
command_select_last_entry_exec(UNUSED void *datap)
{
	view_select_last_entry();
}

/* ARGSUSED */
static void
command_select_next_entry_exec(UNUSED void *datap)
{
	view_select_next_entry();
}

/* ARGSUSED */
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
	else if (!strcmp(argv[1], "queue"))
		*data = VIEW_ID_QUEUE;
	else {
		(void)xasprintf(error, "Invalid view: %s", argv[1]);
		free(data);
		return -1;
	}

	*datap = data;
	return 0;
}

/* ARGSUSED */
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
		(void)xasprintf(error, "Invalid option: %s", argv[1]);
		goto error;
	}

	if (argc == 2 && data->type != OPTION_TYPE_BOOLEAN) {
		(void)xasprintf(error, "Cannot toggle option: %s", argv[1]);
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
				(void)xasprintf(error, "Invalid attribute: %s",
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
			(void)xasprintf(error, "Invalid boolean: %s", argv[2]);
			goto error;
		}
		break;
	case OPTION_TYPE_COLOUR:
		if (option_string_to_colour(argv[2], &data->value.colour) ==
		    -1) {
			(void)xasprintf(error, "Invalid colour: %s", argv[2]);
			goto error;
		}
		break;
	case OPTION_TYPE_NUMBER:
		option_get_number_range(argv[1], &min, &max);
		data->value.number = (int)strtonum(argv[2], min, max, &errstr);
		if (errstr != NULL) {
			(void)xasprintf(error, "Number is %s: %s", errstr,
			    argv[2]);
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

	if (argc - optind != 1)
		goto usage;

	data->volume = (int)strtonum(argv[optind], 0, 100, &errstr);
	if (errstr != NULL) {
		(void)xasprintf(error, "Volume level is %s: %s", errstr,
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

/* ARGSUSED */
static void
command_stop_exec(UNUSED void *datap)
{
	player_stop();
}

static int
command_string_to_argv(const char *line, char ***argv, char **error)
{
	size_t	 argsize;
	int	 argc, backslash, done, i;
	char	*arg;
	enum {
		QUOTE_NONE,
		QUOTE_DOUBLE,
		QUOTE_SINGLE
	} quote;

	arg = NULL;
	*argv = NULL;
	argsize = 0;
	argc = backslash = done = i = 0;
	quote = QUOTE_NONE;

	while (!done) {
		if (quote == QUOTE_NONE) {
			if (backslash) {
				switch (line[i]) {
				case '\0':
					/* Syntax error. */
					*error = xstrdup("Syntax error: "
					    "backslash at end of line");
					done = 1;
					break;
				case '\\':
				case '\'':
				case '"':
				case ' ':
				case '#':
					arg = xrealloc(arg, argsize + 1);
					arg[argsize++] = line[i];
					backslash = 0;
					break;
				default:
					arg = xrealloc(arg, argsize + 2);
					arg[argsize++] = '\\';
					arg[argsize++] = line[i];
					backslash = 0;
					break;
				}
			} else {
				switch (line[i]) {
				case '\\':
					backslash = 1;
					break;
				case '\'':
					quote = QUOTE_SINGLE;
					break;
				case '"':
					quote = QUOTE_DOUBLE;
					break;
				case '\0':
				case '#':
					done = 1;
					/* FALLTHROUGH */
				case ' ':
				case '\t':
					if (argsize) {
						arg = xrealloc(arg,
						    argsize + 1);
						arg[argsize] = '\0';

						*argv = xrecalloc(*argv,
						    argc + 1, sizeof(char *));
						(*argv)[argc++] = arg;

						arg = NULL;
						argsize = 0;
					}
					break;
				default:
					arg = xrealloc(arg, argsize + 1);
					arg[argsize++] = line[i];
					break;
				}
			}
		} else {
			if (line[i] == '\0') {
				/* Syntax error. */
				*error = xstrdup("Syntax error: missing "
				    "quotation mark");
				done = 1;
			} else if (!backslash) {
				if ((quote == QUOTE_SINGLE && line[i] == '\'')
				    || (quote == QUOTE_DOUBLE && line[i] ==
				    '"'))
					quote = QUOTE_NONE;
				else if (line[i] == '\\')
					backslash = 1;
				else {
					arg = xrealloc(arg, argsize + 1);
					arg[argsize++] = line[i];
				}
			} else {
				if ((quote == QUOTE_SINGLE && line[i] == '\'')
				    || (quote == QUOTE_DOUBLE && line[i] ==
				    '"')) {
					arg = xrealloc(arg, argsize + 1);
					arg[argsize++] = line[i];
				} else {
					arg = xrealloc(arg, argsize + 2);
					arg[argsize++] = '\\';
					arg[argsize++] = line[i];
				}
				backslash = 0;
			}
		}
		i++;
	}

	if (backslash || quote != QUOTE_NONE) {
		command_free_argv(argc, *argv);
		free(arg);
		return -1;
	}

	return argc;
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
		(void)xasprintf(error, "Invalid scope: %s", argv[1]);
		free(data);
		return -1;
	}

	if ((data->key = bind_string_to_key(argv[2])) == K_NONE) {
		(void)xasprintf(error, "Invalid key: %s", argv[2]);
		free(data);
		return -1;
	}

	*datap = data;
	return 0;
}
