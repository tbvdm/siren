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
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "config.h"

/*
 * GCC attributes.
 */

#ifndef __GNUC__
#define NONNULL(...)
#define NORETURN
#define PRINTFLIKE(...)
#define UNUSED
#else
#define NORETURN		__attribute__((noreturn))
#define PRINTFLIKE(fmt, arg)	__attribute__((format(printf, fmt, arg)))
#define UNUSED			__attribute__((unused))

/* The nonnull attribute is available since gcc 3.3. */
#if __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 3)
#define NONNULL(...)
#else
#define NONNULL(...)		__attribute__((nonnull(__VA_ARGS__)))
#endif
#endif

/* File paths. */
#define CONF_DIR		".siren"
#define CACHE_DAT_FILE		"metadata.dat"
#define CACHE_IDX_FILE		"metadata.idx"
#define CONF_FILE		"config"
#define LIBRARY_FILE		"library"

/* Return values of input plug-in functions. */
#define IP_ERROR_PLUGIN		-1
#define IP_ERROR_SYSTEM		-2

/* Priority of output plug-ins. */
#define OP_PRIORITY_SNDIO	1
#define OP_PRIORITY_PULSE	2
#define OP_PRIORITY_AO		3

/* Size of the buffer to be passed to strerror_r(). The value is arbitrary. */
#define STRERROR_BUFSIZE	256

/* Character attributes. */
#define ATTRIB_NORMAL		0x0
#define ATTRIB_BLINK		0x1
#define ATTRIB_BOLD		0x2
#define ATTRIB_DIM		0x4
#define ATTRIB_REVERSE		0x8
#define ATTRIB_STANDOUT		0x10
#define ATTRIB_UNDERLINE	0x20

/* Keys. */
#define K_NONE			0x100
#define K_BACKSPACE		0x101
#define K_BACKTAB		0x102
#define K_DELETE		0x103
#define K_DOWN			0x104
#define K_END			0x105
#define K_ENTER			0x106
#define K_ESCAPE		0x107
#define K_HOME			0x108
#define K_INSERT		0x109
#define K_LEFT			0x110
#define K_PAGEDOWN		0x111
#define K_PAGEUP		0x112
#define K_RIGHT			0x113
#define K_TAB			0x114
#define K_UP			0x115
#define K_F1			0x116
#define K_F2			0x117
#define K_F3			0x118
#define K_F4			0x119
#define K_F5			0x120
#define K_F6			0x121
#define K_F7			0x122
#define K_F8			0x123
#define K_F9			0x124
#define K_F10			0x125
#define K_F11			0x126
#define K_F12			0x127
#define K_F13			0x128
#define K_F14			0x129
#define K_F15			0x130
#define K_F16			0x131
#define K_F17			0x132
#define K_F18			0x133
#define K_F19			0x134
#define K_F20			0x135

/* Whether a character is a control character. */
#define K_IS_CTRL(c)		(((c) & ~0x1F) == 0 || (c) == 0x7F)

/*
 * Convert a control character to its matching printable character and vice
 * versa. For example, convert the ^A control character to "A". Conversion in
 * both directions is done by negating the 7th bit.
 */
#define K_CTRL(c)		((~(c) & 0x40) | ((c) & 0xBF))
#define K_UNCTRL(c)		K_CTRL(c)

/* Time conversion macros. */
#define HOURS(s)		((s) / 3600)
#define MINS(s)			((s) / 60)
#define MSECS(s)		((s) % 60)
#define HMINS(s)		(MINS(s) % 60)

/* Traverse each entry of a menu. */
#define MENU_FOR_EACH_ENTRY(menu, entry)				\
	for ((entry) = menu_get_first_entry(menu);			\
	    (entry) != NULL;						\
	    (entry) = menu_get_next_entry(entry))

/* Number of elements in an array. */
#define NELEMENTS(x)		(sizeof (x) / sizeof (x)[0])

/*
 * Wrappers for log functions.
 */

#define LOG_ERR(...)		log_err(__func__, __VA_ARGS__)
#define LOG_ERRX(...)		log_errx(__func__, __VA_ARGS__)
#define LOG_FATAL(...)		log_fatal(__func__, __VA_ARGS__)
#define LOG_FATALX(...)		log_fatalx(__func__, __VA_ARGS__)
#define LOG_INFO(...)		log_info(__func__, __VA_ARGS__)

#ifndef DEBUG
#define LOG_DEBUG(...)
#define LOG_DEBUG_ERR(...)
#define LOG_DEBUG_ERRX(...)
#else
#define LOG_DEBUG(...)		log_info(__func__, __VA_ARGS__)
#define LOG_DEBUG_ERR(...)	log_err(__func__, __VA_ARGS__)
#define LOG_DEBUG_ERRX(...)	log_errx(__func__, __VA_ARGS__)
#endif

/*
 * Wrappers for xfpathconf(), xpathconf() and xsysconf().
 */

#define XFPATHCONF(fd, name)	xfpathconf(fd, name, #name)
#define XPATHCONF(path, name)	xpathconf(path, name, #name)
#define XSYSCONF(name)		xsysconf(name, #name)

/*
 * Wrappers for pthreads functions.
 */

#ifndef DEBUG
#define XPTHREAD_WRAPPER(func, ...) (void)pthread_ ## func(__VA_ARGS__)
#else
#define XPTHREAD_WRAPPER(func, ...)					\
	do								\
		if ((errno = pthread_ ## func(__VA_ARGS__)) != 0)	\
			LOG_FATAL("pthread_" #func);			\
	while (0)
#endif

#define XPTHREAD_COND_BROADCAST(cond)	XPTHREAD_WRAPPER(cond_broadcast, cond)
#define XPTHREAD_COND_DESTROY(cond)	XPTHREAD_WRAPPER(cond_destroy, cond)
#define XPTHREAD_COND_INIT(cond, attr)	XPTHREAD_WRAPPER(cond_init, cond, attr)
#define XPTHREAD_COND_WAIT(cond, mtx)	XPTHREAD_WRAPPER(cond_wait, cond, mtx)
#define XPTHREAD_CREATE(thd, attr, func, arg) \
	XPTHREAD_WRAPPER(create, thd, attr, func, arg)
#define XPTHREAD_JOIN(thd, ret)		XPTHREAD_WRAPPER(join, thd, ret)
#define XPTHREAD_MUTEX_LOCK(mtx)	XPTHREAD_WRAPPER(mutex_lock, mtx)
#define XPTHREAD_MUTEX_UNLOCK(mtx)	XPTHREAD_WRAPPER(mutex_unlock, mtx)

/* Scopes for key bindings. */
enum bind_scope {
	BIND_SCOPE_COMMON,
	BIND_SCOPE_BROWSER,
	BIND_SCOPE_LIBRARY,
	BIND_SCOPE_MENU,
	BIND_SCOPE_PROMPT,
	BIND_SCOPE_QUEUE
};

enum byte_order {
	BYTE_ORDER_BIG,
	BYTE_ORDER_LITTLE
};

enum colour {
	COLOUR_BLACK,
	COLOUR_BLUE,
	COLOUR_CYAN,
	COLOUR_DEFAULT,
	COLOUR_GREEN,
	COLOUR_MAGENTA,
	COLOUR_RED,
	COLOUR_WHITE,
	COLOUR_YELLOW
};

enum file_type {
	FILE_TYPE_DIRECTORY,
	FILE_TYPE_REGULAR,
	FILE_TYPE_OTHER
};

enum menu_scroll {
	MENU_SCROLL_HALF_PAGE,
	MENU_SCROLL_LINE,
	MENU_SCROLL_PAGE
};

enum option_type {
	OPTION_TYPE_ATTRIB,
	OPTION_TYPE_BOOLEAN,
	OPTION_TYPE_COLOUR,
	OPTION_TYPE_NUMBER,
	OPTION_TYPE_STRING
};

enum view_id {
	VIEW_ID_BROWSER,
	VIEW_ID_LIBRARY,
	VIEW_ID_QUEUE
};

struct command;

struct history;

struct menu;

struct menu_entry;

struct dir;

struct dir_entry {
	char		*name;
	char		*path;
	size_t		 pathsize;
	enum file_type	 type;
};

struct format_field {
	char		 spec;
	const char	*value;
};

struct sample_format {
	enum byte_order	 byte_order;
	unsigned int	 nbits;
	unsigned int	 nchannels;
	unsigned int	 rate;
};

struct track {
	char		*path;

	const struct ip	*ip;
	void		*ipdata;

	char		*album;
	char		*artist;
	char		*date;
	char		*genre;
	char		*title;
	char		*track;
	unsigned int	 duration;

	struct sample_format format;

	unsigned int	 nrefs;
};

/* Input plug-in. */
struct ip {
	const char	 *name;
	const char	**extensions;
	void		  (*close)(struct track *) NONNULL();
	int		  (*get_metadata)(struct track *, char **) NONNULL();
	int		  (*get_position)(struct track *, unsigned int *,
			    char **) NONNULL();
	int		  (*open)(struct track *, char **) NONNULL();
	int		  (*read)(struct track *, int16_t *, size_t, char **)
			    NONNULL();
	int		  (*seek)(struct track *, unsigned int, char **)
			    NONNULL();
};

/* Output plug-in. */
struct op {
	const char	*name;
	const int	 priority;
	void		 (*close)(void);
	const char	*(*error)(int);
	size_t		 (*get_buffer_size)(void);
	int		 (*get_volume)(void);
	int		 (*get_volume_support)(void);
	void		 (*init)(void);
	int		 (*open)(void);
	int		 (*set_volume)(unsigned int);
	int		 (*start)(struct sample_format *) NONNULL();
	int		 (*stop)(void);
	int		 (*write)(void *, size_t) NONNULL();
};

void		 bind_end(void);
int		 bind_execute(enum bind_scope, int);
void		 bind_init(void);
void		 bind_set(enum bind_scope, int, struct command *, void *,
		    const char *) NONNULL();
int		 bind_string_to_scope(const char *, enum bind_scope *)
		    NONNULL();
int		 bind_string_to_key(const char *) NONNULL();
int		 bind_unset(enum bind_scope, int);

void		 browser_activate_entry(void);
void		 browser_change_dir(const char *);
void		 browser_copy_entry(enum view_id);
void		 browser_end(void);
void		 browser_init(void);
void		 browser_print(void);
void		 browser_refresh_dir(void);
void		 browser_search_next(const char *);
void		 browser_search_prev(const char *);
void		 browser_scroll_down(enum menu_scroll);
void		 browser_scroll_up(enum menu_scroll);
void		 browser_select_first_entry(void);
void		 browser_select_last_entry(void);
void		 browser_select_next_entry(void);
void		 browser_select_prev_entry(void);

void		 cache_add_metadata(const struct track *) NONNULL();
void		 cache_end(void);
int		 cache_get_metadata(struct track *) NONNULL();
void		 cache_init(void);

void		 command_execute(struct command *, void *) NONNULL(1);
void		 command_free_data(struct command *, void *) NONNULL(1);
int		 command_parse_string(const char *, struct command **, void **,
		    char **) NONNULL();
int		 command_process(const char *, char **) NONNULL();

void		 conf_end(void);
void		 conf_init(const char *);
char		*conf_path(const char *) NONNULL();
void		 conf_read_file(void);

void		 dir_close(struct dir *) NONNULL();
int		 dir_get_entry(struct dir *, struct dir_entry **)
		    NONNULL();
int		 dir_get_track(struct dir *, struct track **) NONNULL();
struct dir	*dir_open(const char *);

int		 format_snprintf(char *, size_t, const char *,
		    const struct format_field *, size_t) NONNULL();

void		 history_add(struct history *, const char *) NONNULL();
void		 history_clear(struct history *) NONNULL();
void		 history_free(struct history *) NONNULL();
const char	*history_get_next(struct history *) NONNULL();
const char	*history_get_prev(struct history *) NONNULL();
struct history	*history_init(void);
void		 history_resize(struct history *, unsigned int) NONNULL();
void		 history_rewind(struct history *) NONNULL();

void		 library_activate_entry(void);
void		 library_add_track(struct track *) NONNULL();
void		 library_copy_entry(enum view_id);
void		 library_delete_all_entries(void);
void		 library_delete_entry(void);
void		 library_end(void);
struct track	*library_get_next_track(void);
struct track	*library_get_prev_track(void);
void		 library_init(void);
void		 library_print(void);
void		 library_read_file(void);
void		 library_search_next(const char *);
void		 library_search_prev(const char *);
void		 library_scroll_down(enum menu_scroll);
void		 library_scroll_up(enum menu_scroll);
void		 library_select_active_entry(void);
void		 library_select_first_entry(void);
void		 library_select_last_entry(void);
void		 library_select_next_entry(void);
void		 library_select_prev_entry(void);
int		 library_write_file(void);

void		 log_end(void);
void		 log_err(const char *, const char *, ...) PRINTFLIKE(2, 3);
void		 log_errx(const char *, const char *, ...) PRINTFLIKE(2, 3);
void		 log_fatal(const char *, const char *, ...) NORETURN
		    PRINTFLIKE(2, 3);
void		 log_fatalx(const char *, const char *, ...) NORETURN
		    PRINTFLIKE(2, 3);
void		 log_info(const char *, const char *, ...) PRINTFLIKE(2, 3);
void		 log_init(int);

void		 menu_activate_entry(struct menu *, struct menu_entry *)
		    NONNULL();
void		 menu_clear(struct menu *) NONNULL();
void		 menu_free(struct menu *) NONNULL();
struct menu_entry *menu_get_active_entry(const struct menu *) NONNULL();
void		*menu_get_entry_data(const struct menu_entry *) NONNULL();
struct menu_entry *menu_get_first_entry(const struct menu *) NONNULL();
struct menu_entry *menu_get_last_entry(const struct menu *) NONNULL();
unsigned int	 menu_get_nentries(const struct menu *) NONNULL();
struct menu_entry *menu_get_next_entry(const struct menu_entry *) NONNULL();
struct menu_entry *menu_get_prev_entry(const struct menu_entry *) NONNULL();
struct menu_entry *menu_get_selected_entry(const struct menu *) NONNULL();
void		*menu_get_selected_entry_data(const struct menu *) NONNULL();
struct menu	*menu_init(void (*)(void *),
		    void (*)(const void *, char *, size_t),
		    int (*)(const void *, const char *));
void		 menu_insert_before(struct menu *, struct menu_entry *,
		    void *) NONNULL();
void		 menu_insert_tail(struct menu *, void *) NONNULL();
void		 menu_move_entry_down(struct menu_entry *) NONNULL();
void		 menu_move_entry_up(struct menu_entry *) NONNULL();
void		 menu_print(struct menu *) NONNULL();
void		 menu_remove_first_entry(struct menu *) NONNULL();
void		 menu_remove_selected_entry(struct menu *) NONNULL();
void		 menu_scroll_down(struct menu *, enum menu_scroll) NONNULL();
void		 menu_scroll_up(struct menu *, enum menu_scroll) NONNULL();
void		 menu_search_next(struct menu *, const char *) NONNULL();
void		 menu_search_prev(struct menu *, const char *) NONNULL();
void		 menu_select_active_entry(struct menu *) NONNULL();
void		 menu_select_entry(struct menu *, struct menu_entry *)
		    NONNULL();
void		 menu_select_first_entry(struct menu *) NONNULL();
void		 menu_select_last_entry(struct menu *) NONNULL();
void		 menu_select_next_entry(struct menu *) NONNULL();
void		 menu_select_prev_entry(struct menu *) NONNULL();

void		 msg_clear(void);
void		 msg_err(const char *, ...) PRINTFLIKE(1, 2);
void		 msg_errx(const char *, ...) PRINTFLIKE(1, 2);
void		 msg_info(const char *, ...) PRINTFLIKE(1, 2);
void		 msg_ip_err(int, const char *, const char *, ...)
		    NONNULL() PRINTFLIKE(3, 4);
void		 msg_op_err(const struct op *, int, const char *, ...)
		    NONNULL() PRINTFLIKE(3, 4);

void		 option_add_boolean(const char *, int, void (*)(void))
		    NONNULL(1);
void		 option_add_number(const char *, int, int, int, void (*)(void))
		    NONNULL(1);
void		 option_add_string(const char *, const char *,
		    void (*)(void)) NONNULL(1, 2);
void		 option_end(void);
int		 option_get_attrib(const char *) NONNULL();
int		 option_get_boolean(const char *) NONNULL();
enum colour	 option_get_colour(const char *) NONNULL();
int		 option_get_number(const char *) NONNULL();
void		 option_get_number_range(const char *, int *, int *) NONNULL();
char		*option_get_string(const char *) NONNULL();
int		 option_get_type(const char *, enum option_type *) NONNULL();
void		 option_init(void);
void		 option_set_attrib(const char *, int) NONNULL();
void		 option_set_boolean(const char *, int) NONNULL();
void		 option_set_colour(const char *, enum colour) NONNULL();
void		 option_set_number(const char *, int) NONNULL();
void		 option_set_string(const char *, const char *) NONNULL();
int		 option_string_to_attrib(const char *) NONNULL();
int		 option_string_to_boolean(const char *) NONNULL();
int		 option_string_to_colour(const char *, enum colour *)
		    NONNULL();
void		 option_toggle_boolean(const char *) NONNULL();

char		*path_get_cwd(void);
char		*path_get_home_dir(const char *);
char		*path_normalise(const char *) NONNULL();

void		 player_change_op(void);
void		 player_end(void);
enum byte_order	 player_get_byte_order(void);
void		 player_init(void);
void		 player_pause(void);
void		 player_play(void);
void		 player_play_next(void);
void		 player_play_prev(void);
void		 player_play_track(struct track *) NONNULL();
void		 player_print(void);
void		 player_seek(int, int);
void		 player_set_volume(int, int);
void		 player_stop(void);

void		 plugin_end(void);
void		 plugin_init(void);
const struct ip	*plugin_find_ip(const char *) NONNULL();
const struct op	*plugin_find_op(const char *) NONNULL();

void		 prompt_clear_command_history(void);
void		 prompt_clear_search_history(void);
void		 prompt_end(void);
int		 prompt_get_answer(const char *) NONNULL();
char		*prompt_get_command(const char *) NONNULL();
char		*prompt_get_search(const char *) NONNULL();
void		 prompt_init(void);
int		 prompt_is_active(void);
void		 prompt_print(void);
void		 prompt_resize_histories(void);

void		 queue_activate_entry(void);
void		 queue_add_track(struct track *) NONNULL();
void		 queue_copy_entry(enum view_id);
void		 queue_delete_all_entries(void);
void		 queue_delete_entry(void);
void		 queue_end(void);
struct track	*queue_get_next_track(void);
void		 queue_init(void);
void		 queue_move_entry_down(void);
void		 queue_move_entry_up(void);
void		 queue_print(void);
void		 queue_scroll_down(enum menu_scroll);
void		 queue_scroll_up(enum menu_scroll);
void		 queue_search_next(const char *);
void		 queue_search_prev(const char *);
void		 queue_select_first_entry(void);
void		 queue_select_last_entry(void);
void		 queue_select_next_entry(void);
void		 queue_select_prev_entry(void);

void		 screen_configure_cursor(void);
void		 screen_configure_objects(void);
void		 screen_end(void);
int		 screen_get_key(void);
unsigned int	 screen_get_ncols(void);
void		 screen_init(void);
void		 screen_msg_error_printf(const char *, ...) NONNULL()
		    PRINTFLIKE(1, 2);
void		 screen_msg_error_vprintf(const char *, va_list) NONNULL()
		    PRINTFLIKE(1, 0);
void		 screen_msg_info_vprintf(const char *, va_list) NONNULL()
		    PRINTFLIKE(1, 0);
void		 screen_player_status_printf(const char *, ...) NONNULL()
		    PRINTFLIKE(1, 2);
void		 screen_player_track_print(const char *) NONNULL();
void		 screen_print(void);
void		 screen_prompt_begin(void);
void		 screen_prompt_end(void);
void		 screen_prompt_printf(size_t, const char *, ...) NONNULL()
		    PRINTFLIKE(2, 3);
void		 screen_refresh(void);
void		 screen_status_clear(void);
unsigned int	 screen_view_get_nrows(void);
void		 screen_view_print(const char *) NONNULL();
void		 screen_view_print_active(const char *) NONNULL();
void		 screen_view_print_begin(void);
void		 screen_view_print_end(void);
void		 screen_view_print_selected(const char *s) NONNULL();
void		 screen_view_title_printf(const char *, ...) PRINTFLIKE(1, 2);
void		 screen_view_title_printf_right(const char *, ...)
		    PRINTFLIKE(1, 2);

int		 track_cmp(const struct track *, const struct track *)
		    NONNULL();
void		 track_free(struct track *);
void		 track_hold(struct track *) NONNULL();
struct track	*track_init(const char *, const struct ip *) NONNULL(1);
int		 track_search(const struct track *, const char *);
int		 track_snprintf(char *, size_t, const char *,
		    const struct track *) NONNULL();

void		 view_activate_entry(void);
void		 view_add_track(enum view_id, struct track *);
void		 view_copy_entry(enum view_id);
void		 view_copy_track(enum view_id, enum view_id, struct track *);
void		 view_delete_all_entries(void);
void		 view_delete_entry(void);
enum bind_scope	 view_get_bind_scope(void);
enum view_id	 view_get_id(void);
void		 view_move_entry_down(void);
void		 view_move_entry_up(void);
void		 view_print(void);
void		 view_scroll_down(enum menu_scroll);
void		 view_scroll_up(enum menu_scroll);
void		 view_search_next(const char *);
void		 view_search_prev(const char *);
void		 view_select_first_entry(void);
void		 view_select_last_entry(void);
void		 view_select_next_entry(void);
void		 view_select_prev_entry(void);
void		 view_select_view(enum view_id);

int		 xasprintf(char **, const char *, ...) NONNULL()
		    PRINTFLIKE(2, 3);
void		*xcalloc(size_t, size_t);
void		*xmalloc(size_t);
void		*xrealloc(void *, size_t);
void		*xrecalloc(void *, size_t, size_t);
int		 xsnprintf(char *, size_t, const char *, ...) PRINTFLIKE(3, 4);
char		*xstrdup(const char *) NONNULL();
int		 xvasprintf(char **, const char *, va_list) NONNULL()
		    PRINTFLIKE(2, 0);
int		 xvsnprintf(char *, size_t, const char *, va_list) NONNULL()
		    PRINTFLIKE(3, 0);

long int	 xfpathconf(int, int, const char *) NONNULL();
long int	 xpathconf(const char *, int, const char *) NONNULL();
long int	 xsysconf(int, const char *) NONNULL();

#ifndef HAVE_ASPRINTF
int		 asprintf(char **, const char *, ...) NONNULL()
		    PRINTFLIKE(2, 3);
int		 vasprintf(char **, const char *, va_list) NONNULL()
		    PRINTFLIKE(2, 0);
#endif

#ifndef HAVE_ERR
void		 err(int, const char *, ...) NORETURN PRINTFLIKE(2, 3);
void		 errx(int, const char *, ...) NORETURN PRINTFLIKE(2, 3);
void		 verr(int, const char *, va_list) NORETURN PRINTFLIKE(2, 0);
void		 verrx(int, const char *, va_list) NORETURN PRINTFLIKE(2, 0);
void		 vwarn(const char *, va_list) PRINTFLIKE(1, 0);
void		 vwarnx(const char *, va_list) PRINTFLIKE(1, 0);
void		 warn(const char *, ...) PRINTFLIKE(1, 2);
void		 warnx(const char *, ...) PRINTFLIKE(1, 2);
#endif

#ifndef HAVE_FGETLN
char		*fgetln(FILE *, size_t *);
#endif

#ifndef HAVE_OPTRESET
#define getopt		xgetopt
#define optarg		xoptarg
#define opterr		xopterr
#define optind		xoptind
#define optopt		xoptopt
#define optreset	xoptreset

extern int	 xopterr, xoptind, xoptopt, xoptreset;
extern char	*xoptarg;

int		 xgetopt(int, char * const *, const char *);
#endif

#ifndef HAVE_STRCASESTR
char		*strcasestr(const char *, const char *);
#endif

#ifndef HAVE_STRLCAT
size_t		 strlcat(char *, const char *, size_t);
#endif

#ifndef HAVE_STRLCPY
size_t		 strlcpy(char *, const char *, size_t);
#endif

#ifndef HAVE_STRSEP
char		*strsep(char **, const char *);
#endif

#ifndef HAVE_STRTONUM
long long int	 strtonum(const char *, long long, long long, const char **);
#endif
