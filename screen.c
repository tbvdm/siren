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

#include <curses.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "siren.h"

#ifdef HAVE_RESIZETERM
#include <sys/ioctl.h>
#endif

#define SCREEN_OBJ_ACTIVE	0
#define SCREEN_OBJ_ERROR	1
#define SCREEN_OBJ_INFO		2
#define SCREEN_OBJ_PLAYER	3
#define SCREEN_OBJ_PROMPT	4
#define SCREEN_OBJ_SELECTOR	5
#define SCREEN_OBJ_STATUS	6
#define SCREEN_OBJ_TITLE	7
#define SCREEN_OBJ_VIEW		8

#define SCREEN_PLAYER_NROWS	2
#define SCREEN_STATUS_NROWS	1
#define SCREEN_TITLE_NROWS	1

#define SCREEN_TITLE_ROW	0
#define SCREEN_VIEW_ROW		1

static short int		 screen_get_colour(const char *, int);
static void			 screen_msg_vprintf(int, const char *,
				    va_list);
static void			 screen_print_row(const char *);
static void			 screen_view_print_row(chtype, const char *);
static void			 screen_vprintf(const char *, va_list);
#if defined(HAVE_RESIZETERM) && defined(TIOCGWINSZ)
static void			 screen_resize(void);
#endif

static pthread_mutex_t		 screen_curses_mtx = PTHREAD_MUTEX_INITIALIZER;
static int			 screen_have_colours;
static int			 screen_player_row;
static int			 screen_status_col;
static int			 screen_status_row;
static int			 screen_view_current_row;
static int			 screen_view_selected_row;
static int			 screen_view_nrows;

static char			*screen_row = NULL;
static size_t			 screen_rowsize;

#ifdef HAVE_USE_DEFAULT_COLORS
static int			 screen_have_default_colours;
#endif

static const struct {
	const int		 attrib;
	const chtype		 curses_attrib;
} screen_attribs[] = {
	{ ATTRIB_BLINK,		 A_BLINK },
	{ ATTRIB_BOLD,		 A_BOLD },
	{ ATTRIB_DIM,		 A_DIM },
	{ ATTRIB_REVERSE,	 A_REVERSE },
	{ ATTRIB_STANDOUT,	 A_STANDOUT },
	{ ATTRIB_UNDERLINE,	 A_UNDERLINE }
};

static const struct {
	const enum colour	 colour;
	const short int		 curses_colour;
} screen_colours[] = {
	{ COLOUR_BLACK,		 COLOR_BLACK },
	{ COLOUR_BLUE,		 COLOR_BLUE },
	{ COLOUR_CYAN,		 COLOR_CYAN },
	{ COLOUR_DEFAULT,	 -1 },
	{ COLOUR_GREEN,		 COLOR_GREEN },
	{ COLOUR_MAGENTA,	 COLOR_MAGENTA },
	{ COLOUR_RED,		 COLOR_RED },
	{ COLOUR_WHITE,		 COLOR_WHITE },
	{ COLOUR_YELLOW,	 COLOR_YELLOW }
};

static const struct {
	const int		 key;
	const int		 curses_key;
} screen_keys[] = {
	{ K_BACKSPACE,		'\b' },
	{ K_BACKSPACE,		'\177' /* ^? */ },
	{ K_BACKSPACE,		KEY_BACKSPACE },
	{ K_BACKTAB,		KEY_BTAB },
	{ K_DELETE,		KEY_DC },
	{ K_DOWN,		KEY_DOWN },
	{ K_END,		KEY_END },
	{ K_ENTER,		'\n' },
	{ K_ENTER,		'\r' },
	{ K_ENTER,		KEY_ENTER },
	{ K_ESCAPE,		K_CTRL('[') },
	{ K_F1,			KEY_F(1) },
	{ K_F2,			KEY_F(2) },
	{ K_F3,			KEY_F(3) },
	{ K_F4,			KEY_F(4) },
	{ K_F5,			KEY_F(5) },
	{ K_F6,			KEY_F(6) },
	{ K_F7,			KEY_F(7) },
	{ K_F8,			KEY_F(8) },
	{ K_F9,			KEY_F(9) },
	{ K_F10,		KEY_F(10) },
	{ K_F11,		KEY_F(11) },
	{ K_F12,		KEY_F(12) },
	{ K_F13,		KEY_F(13) },
	{ K_F14,		KEY_F(14) },
	{ K_F15,		KEY_F(15) },
	{ K_F16,		KEY_F(16) },
	{ K_F17,		KEY_F(17) },
	{ K_F18,		KEY_F(18) },
	{ K_F19,		KEY_F(19) },
	{ K_F20,		KEY_F(20) },
	{ K_HOME,		KEY_HOME },
	{ K_INSERT,		KEY_IC },
	{ K_LEFT,		KEY_LEFT },
	{ K_PAGEDOWN,		KEY_NPAGE },
	{ K_PAGEUP,		KEY_PPAGE },
	{ K_RIGHT,		KEY_RIGHT },
	{ K_TAB,		'\t' },
	{ K_UP,			KEY_UP }
};

static struct {
	chtype			 attr;
	const short int		 colour_pair;
	const char		*option_attr;
	const char		*option_bg;
	const char		*option_fg;
} screen_objects[] = {
	{ A_NORMAL, 1, "active-attr",     "active-bg",     "active-fg" },
	{ A_NORMAL, 2, "error-attr",      "error-bg",      "error-fg" },
	{ A_NORMAL, 3, "info-attr",       "info-bg",       "info-fg" },
	{ A_NORMAL, 4, "player-attr",     "player-bg",     "player-fg" },
	{ A_NORMAL, 5, "prompt-attr",     "prompt-bg",     "prompt-fg" },
	{ A_NORMAL, 6, "selection-attr",  "selection-bg",  "selection-fg" },
	{ A_NORMAL, 7, "status-attr",     "status-bg",     "status-fg" },
	{ A_NORMAL, 8, "view-title-attr", "view-title-bg", "view-title-fg" },
	{ A_NORMAL, 9, "view-attr",       "view-bg",       "view-fg" }
};

static void
screen_configure_attribs(void)
{
	size_t	 i, j;
	int	 attr;
	chtype	 cattr;

	for (i = 0; i < NELEMENTS(screen_objects); i++) {
		attr = option_get_attrib(screen_objects[i].option_attr);
		cattr = A_NORMAL;

		for (j = 0; j < NELEMENTS(screen_attribs); j++)
			if (attr & screen_attribs[j].attrib)
				cattr |= screen_attribs[j].curses_attrib;

		XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
		screen_objects[i].attr = cattr;
		XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	}
}

static void
screen_configure_colours(void)
{
	size_t		i;
	short int	bg, fg;

	if (!screen_have_colours)
		return;

	for (i = 0; i < NELEMENTS(screen_objects); i++) {
		bg = screen_get_colour(screen_objects[i].option_bg,
		    COLOUR_BLACK);
		fg = screen_get_colour(screen_objects[i].option_fg,
		    COLOUR_WHITE);

		XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
		if (init_pair(screen_objects[i].colour_pair, fg, bg) == OK)
			screen_objects[i].attr |=
			    COLOR_PAIR(screen_objects[i].colour_pair);
		XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	}
}

void
screen_configure_cursor(void)
{
	int show;

	show = option_get_boolean("show-cursor");
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	curs_set(show);
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_configure_objects(void)
{
	screen_configure_attribs();
	screen_configure_colours();
	screen_print();
}

static void
screen_configure_rows(void)
{
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);

	/* Calculate the number of rows available to the view area. */
	if (LINES < SCREEN_TITLE_NROWS + SCREEN_PLAYER_NROWS +
	    SCREEN_STATUS_NROWS)
		screen_view_nrows = 0;
	else
		screen_view_nrows = LINES - SCREEN_TITLE_NROWS -
		    SCREEN_PLAYER_NROWS - SCREEN_STATUS_NROWS;

	/* Calculate the row offsets of the player and status areas. */
	screen_player_row = SCREEN_TITLE_NROWS + screen_view_nrows;
	screen_status_row = screen_player_row + SCREEN_PLAYER_NROWS;

	/* (Re)allocate memory for the row buffer. */
	if (screen_rowsize != (size_t)COLS + 1) {
		screen_rowsize = COLS + 1;
		screen_row = xrealloc(screen_row, screen_rowsize);
	}

	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_end(void)
{
	endwin();
	free(screen_row);
}

static short int
screen_get_colour(const char *option, enum colour default_colour)
{
	size_t		i;
	int		colour;

	colour = option_get_colour(option);

	if (colour >= 0 && colour < COLORS)
		return colour;

#ifdef HAVE_USE_DEFAULT_COLORS
	if (colour == COLOUR_DEFAULT && !screen_have_default_colours)
#else
	if (colour == COLOUR_DEFAULT)
#endif
		colour = default_colour;

	for (i = 0; i < NELEMENTS(screen_colours); i++)
		if (colour == screen_colours[i].colour)
			return screen_colours[i].curses_colour;

	LOG_FATALX("unknown colour: %d", colour);
}

int
screen_get_key(void)
{
	size_t	i;
	int	key;

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	while ((key = getch()) == ERR && errno == EINTR);
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);

	if (key != ERR) {
		for (i = 0; i < NELEMENTS(screen_keys); i++)
			if (key == screen_keys[i].curses_key)
				return screen_keys[i].key;

		/* Only allow ASCII characters. */
		if (key > -1 && key < 128)
			return key;
	}

	return K_NONE;
}

int
screen_get_ncolours(void)
{
	return screen_have_colours ? COLORS : 0;
}

unsigned int
screen_get_ncols(void)
{
	return COLS;
}

void
screen_init(void)
{
	if (initscr() == NULL)
		LOG_FATALX("cannot initialise screen");

	cbreak();
	noecho();
	nonl();
	keypad(stdscr, TRUE);

	if (has_colors() == TRUE) {
		if (start_color() == ERR)
			LOG_ERRX("start_color() failed");
		else {
			screen_have_colours = 1;
			LOG_INFO("terminal supports %d colours", COLORS);
#ifdef HAVE_USE_DEFAULT_COLORS
			if (use_default_colors() == OK) {
				screen_have_default_colours = 1;
				LOG_INFO("terminal supports default colours");
			}
#endif
		}
	}

	screen_configure_rows();
	screen_configure_cursor();
	screen_configure_attribs();
	screen_configure_colours();
}

void
screen_msg_error_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	screen_msg_vprintf(SCREEN_OBJ_ERROR, fmt, ap);
	va_end(ap);
}

void
screen_msg_error_vprintf(const char *fmt, va_list ap)
{
	screen_msg_vprintf(SCREEN_OBJ_ERROR, fmt, ap);
}

void
screen_msg_info_vprintf(const char *fmt, va_list ap)
{
	screen_msg_vprintf(SCREEN_OBJ_INFO, fmt, ap);
}

static void
screen_msg_vprintf(int obj, const char *fmt, va_list ap)
{
	int col, row;

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	getyx(stdscr, row, col);
	if (move(screen_status_row, 0) == OK) {
		bkgdset(screen_objects[obj].attr);
		screen_vprintf(fmt, ap);
		move(row, col);
		refresh();
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_player_status_printf(const struct format *fmt,
    const struct format_variable *fmtvar, size_t nfmtvars)
{
	int col, row;

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	format_snprintf(screen_row, screen_rowsize, fmt, fmtvar, nfmtvars);
	getyx(stdscr, row, col);
	if (move(screen_player_row + 1, 0) == OK) {
		bkgdset(screen_objects[SCREEN_OBJ_PLAYER].attr);
		screen_print_row(screen_row);
		move(row, col);
		refresh();
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_player_track_printf(const struct format *fmt,
    const struct format *altfmt, const struct track *track)
{
	int col, row;

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	if (track == NULL)
		screen_row[0] = '\0';
	else
		format_track_snprintf(screen_row, screen_rowsize, fmt, altfmt,
		    track);

	getyx(stdscr, row, col);
	if (move(screen_player_row, 0) == OK) {
		bkgdset(screen_objects[SCREEN_OBJ_PLAYER].attr);
		screen_print_row(screen_row);
		move(row, col);
		refresh();
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_print(void)
{
	view_print();
	player_print();
	if (input_get_mode() == INPUT_MODE_PROMPT)
		prompt_print();
	else
		screen_status_clear();
}

/*
 * The screen_curses_mtx mutex must be locked before calling this function.
 */
static void
screen_print_row(const char *s)
{
	int col UNUSED, row;

	addnstr(s, COLS);

	if (strlen(s) < (size_t)COLS)
		clrtoeol();
	else {
		/*
		 * If the length of the printed string is equal to the screen
		 * width, the cursor will advance to the next row. Undo this by
		 * moving the cursor back to the original row.
		 */
		getyx(stdscr, row, col);
		move(row - 1, COLS - 1);
	}
}

void
screen_prompt_begin(void)
{
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	curs_set(1);
	screen_status_col = 0;
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_prompt_end(void)
{
	int show;

	show = option_get_boolean("show-cursor");
	screen_status_clear();
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	if (!show)
		curs_set(0);
	move(screen_view_selected_row, 0);
	refresh();
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_prompt_printf(size_t cursorpos, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	if ((int)cursorpos >= COLS && COLS > 0)
		screen_status_col = COLS - 1;
	else
		screen_status_col = cursorpos;

	if (move(screen_status_row, 0) == OK) {
		bkgdset(screen_objects[SCREEN_OBJ_PROMPT].attr);
		screen_vprintf(fmt, ap);
		move(screen_status_row, screen_status_col);
		refresh();
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	va_end(ap);
}

void
screen_refresh(void)
{
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	clear();
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
#if defined(HAVE_RESIZETERM) && defined(TIOCGWINSZ)
	screen_resize();
#endif
	screen_print();
}

#if defined(HAVE_RESIZETERM) && defined(TIOCGWINSZ)
static void
screen_resize(void)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
		LOG_ERR("ioctl");
		return;
	}

	/*
	 * resizeterm() will fail if the width or height of the terminal window
	 * is not larger than zero.
	 */
	if (ws.ws_col == 0 || ws.ws_row == 0)
		return;

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	if (resizeterm(ws.ws_row, ws.ws_col) == ERR)
		/*
		 * resizeterm() might have failed to allocate memory, so treat
		 * this as fatal.
		 */
		LOG_FATALX("resizeterm() failed");
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);

	screen_configure_rows();
}
#endif

void
screen_status_clear(void)
{
	int col, row;

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	getyx(stdscr, row, col);
	if (move(screen_status_row, 0) == OK) {
		bkgdset(screen_objects[SCREEN_OBJ_STATUS].attr);
		clrtoeol();
		move(row, col);
		refresh();
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

unsigned int
screen_view_get_nrows(void)
{
	return screen_view_nrows;
}

void
screen_view_print(const char *s)
{
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	screen_view_print_row(screen_objects[SCREEN_OBJ_VIEW].attr, s);
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_view_print_active(const char *s)
{
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	screen_view_print_row(screen_objects[SCREEN_OBJ_ACTIVE].attr, s);
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_view_print_begin(void)
{
	int i;

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	/* Clear the view area. */
	bkgdset(screen_objects[SCREEN_OBJ_VIEW].attr);
	for (i = 0; i < screen_view_nrows; i++) {
		move(SCREEN_VIEW_ROW + i, 0);
		clrtoeol();
	}
	screen_view_current_row = screen_view_selected_row = SCREEN_VIEW_ROW;
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_view_print_end(void)
{
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	if (input_get_mode() == INPUT_MODE_PROMPT)
		move(screen_status_row, screen_status_col);
	else
		move(screen_view_selected_row, 0);
	refresh();
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

/*
 * The screen_curses_mtx mutex must be locked before calling this function.
 */
static void
screen_view_print_row(chtype attr, const char *s)
{
	bkgdset(attr);
	move(screen_view_current_row++, 0);
	screen_print_row(s);
}

void
screen_view_print_selected(const char *s)
{
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	screen_view_selected_row = screen_view_current_row;
	screen_view_print_row(screen_objects[SCREEN_OBJ_SELECTOR].attr, s);
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_view_title_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	if (move(SCREEN_TITLE_ROW, 0) == OK) {
		bkgdset(screen_objects[SCREEN_OBJ_TITLE].attr);
		screen_vprintf(fmt, ap);
		/* No refresh() yet; screen_view_print_end() will do that. */
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	va_end(ap);
}

void
screen_view_title_printf_right(const char *fmt, ...)
{
	va_list	ap;
	int	len;

	va_start(ap, fmt);
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	bkgdset(screen_objects[SCREEN_OBJ_TITLE].attr);
	len = xvsnprintf(screen_row, screen_rowsize, fmt, ap);
	mvaddstr(SCREEN_TITLE_ROW, (len < COLS) ? (COLS - len) : 0, screen_row);
	/* No refresh() yet; screen_view_print_end() will do that. */
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	va_end(ap);
}

/*
 * The screen_curses_mtx mutex must be locked before calling this function.
 */
PRINTFLIKE(1, 0) static void
screen_vprintf(const char *fmt, va_list ap)
{
	int col UNUSED, len, row;

	len = xvsnprintf(screen_row, screen_rowsize, fmt, ap);
	addnstr(screen_row, COLS);

	if (len < COLS)
		clrtoeol();
	else {
		/*
		 * If the length of the printed string is equal to the screen
		 * width, the cursor will advance to the next row. Undo this by
		 * moving the cursor back to the original row.
		 */
		getyx(stdscr, row, col);
		move(row - 1, COLS - 1);
	}
}
