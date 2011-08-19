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

#include <ctype.h>
#include <curses.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
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

#ifdef HAVE_NETBSD_CURSES
#define clrtoeol		screen_clrtoeol
#endif

static void			screen_configure_attribs(void);
static void			screen_configure_colours(void);
static short int		screen_get_colour(const char *, enum colour);
static void			screen_msg_vprintf(chtype *, const char *,
				    va_list);
static void			screen_print_row(const char *);
static void			screen_resize(void);
static void			screen_show_cursor(int);
static void			screen_vprintf(const char *, va_list);

#ifdef SIGWINCH
static void			screen_sigwinch_handler(int);
#endif

static pthread_mutex_t		screen_curses_mtx = PTHREAD_MUTEX_INITIALIZER;
static int			screen_player_row;
static int			screen_status_row;
static int			screen_view_cursor_row;
static int			screen_view_nrows;

#ifdef SIGWINCH
static volatile sig_atomic_t	screen_sigwinch;
#endif

#ifdef HAVE_USE_DEFAULT_COLORS
static int			screen_have_default_colours;
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
screen_calculate_rows(void)
{
	if (LINES < SCREEN_TITLE_NROWS + SCREEN_PLAYER_NROWS +
	    SCREEN_STATUS_NROWS)
		screen_view_nrows = 0;
	else
		screen_view_nrows = LINES - SCREEN_TITLE_NROWS -
		    SCREEN_PLAYER_NROWS - SCREEN_STATUS_NROWS;

	screen_player_row = SCREEN_TITLE_NROWS + screen_view_nrows;
	screen_status_row = screen_player_row + SCREEN_PLAYER_NROWS;
}

#ifdef HAVE_NETBSD_CURSES
/*
 * NetBSD's clrtoeol() does not preserve the attributes of the cells it clears
 * (although it does preserve the colour attributes). We work around this by
 * using our own version which simply writes spaces to the end of the row.
 */
static void
screen_clrtoeol(void)
{
	int col, maxcol, row;

	getyx(stdscr, row, col);
	maxcol = getmaxx(stdscr); /* getmaxx() is an extension. */
	while (maxcol-- > col)
		(void)addch(' ');
	(void)move(row, col);
}
#endif

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

		screen_objects[i].attr = cattr;
	}
}

static void
screen_configure_colours(void)
{
	size_t		i;
	short int	bg, fg;

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	for (i = 0; i < NELEMENTS(screen_objects); i++) {
		bg = screen_get_colour(screen_objects[i].option_bg,
		    COLOUR_BLACK);
		fg = screen_get_colour(screen_objects[i].option_fg,
		    COLOUR_WHITE);

		if (init_pair(screen_objects[i].colour_pair, fg, bg) == OK)
			screen_objects[i].attr |=
			    (chtype)COLOR_PAIR(screen_objects[i].colour_pair);
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_configure_cursor(void)
{
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	if (option_get_boolean("show-cursor"))
		(void)curs_set(1);
	else
		(void)curs_set(0);

#ifdef HAVE_NETBSD_CURSES
	(void)refresh();
#endif
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_configure_objects(void)
{
	screen_configure_attribs();
	if (has_colors() == TRUE)
		screen_configure_colours();
	screen_print();
}

#if defined(VDSUSP) && defined(_PC_VDISABLE)
/*
 * Check if the DSUSP special-character is set to ^Y. If it is, disable it so
 * that ^Y becomes an ordinary character that can be bound to a command.
 */
static void
screen_disable_dsusp(void)
{
	struct termios	tio;
	long int	vdisable;

	if ((vdisable = XFPATHCONF(STDIN_FILENO, _PC_VDISABLE)) == -1)
		return;

	if (tcgetattr(STDIN_FILENO, &tio) == -1) {
		LOG_ERR("tcgetattr");
		return;
	}

	if (tio.c_cc[VDSUSP] == K_CTRL('Y')) {
		tio.c_cc[VDSUSP] = (cc_t)vdisable;
		if (tcsetattr(STDIN_FILENO, TCSANOW, &tio) == -1)
			LOG_ERR("tcsetattr");
	}
}
#endif

void
screen_end(void)
{
	(void)endwin();
}

static short int
screen_get_colour(const char *option, enum colour default_colour)
{
	size_t		i;
	enum colour	colour;

	colour = option_get_colour(option);
#ifdef HAVE_USE_DEFAULT_COLORS
	if (colour == COLOUR_DEFAULT && !screen_have_default_colours)
#else
	if (colour == COLOUR_DEFAULT)
#endif
		colour = default_colour;

	for (i = 0; i < NELEMENTS(screen_colours); i++)
		if (colour == screen_colours[i].colour)
			return screen_colours[i].curses_colour;

	LOG_FATALX("unknown colour");
	/* NOTREACHED */
}

int
screen_get_key(void)
{
	struct pollfd	pfd[1];
	size_t		i;
	int		key;

	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN;

	for (;;) {
#ifdef SIGWINCH
		if (screen_sigwinch) {
			screen_sigwinch = 0;
			screen_refresh();
		}
#endif

		if (poll(pfd, 1, -1) == -1) {
			if (errno != EINTR)
				LOG_FATAL("poll");
		} else if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL))
			LOG_FATALX("poll() failed");
		else {
			XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
			while ((key = getch()) == ERR && errno == EINTR);
			XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);

			if (key != ERR) {
				for (i = 0; i < NELEMENTS(screen_keys); i++)
					if (key == screen_keys[i].curses_key)
						return screen_keys[i].key;

				return isascii(key) ? key : K_NONE;
			}
		}
	}
}

unsigned int
screen_get_ncols(void)
{
	return (unsigned int)COLS;
}

void
screen_init(void)
{
#ifdef SIGWINCH
	struct sigaction sa;

	sa.sa_handler = screen_sigwinch_handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGWINCH, &sa, NULL) == -1)
		LOG_ERR("sigaction");
#endif

	(void)initscr();
	(void)cbreak();
	(void)noecho();
	(void)nonl();
	(void)keypad(stdscr, TRUE);

#if defined(VDSUSP) && defined(_PC_VDISABLE)
	screen_disable_dsusp();
#endif

	screen_calculate_rows();
	screen_configure_cursor();
	screen_configure_attribs();

	if (has_colors() == TRUE) {
		if (start_color() == ERR)
			LOG_FATALX("start_color() failed");
#ifdef HAVE_USE_DEFAULT_COLORS
		if (use_default_colors() == OK)
			screen_have_default_colours = 1;
#endif
		screen_configure_colours();
	}
}

void
screen_msg_error_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	screen_msg_vprintf(&screen_objects[SCREEN_OBJ_ERROR].attr, fmt, ap);
	va_end(ap);
}

void
screen_msg_error_vprintf(const char *fmt, va_list ap)
{
	screen_msg_vprintf(&screen_objects[SCREEN_OBJ_ERROR].attr, fmt, ap);
}

void
screen_msg_info_vprintf(const char *fmt, va_list ap)
{
	screen_msg_vprintf(&screen_objects[SCREEN_OBJ_INFO].attr, fmt, ap);
}

static void
screen_msg_vprintf(chtype *attr, const char *fmt, va_list ap)
{
	int col, row;

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	getyx(stdscr, row, col);
	if (move(screen_status_row, 0) == OK) {
		bkgdset(*attr);
		screen_vprintf(fmt, ap);
		(void)move(row, col);
		(void)refresh();
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_player_status_printf(const char *fmt, ...)
{
	va_list	ap;
	int	col, row;

	va_start(ap, fmt);
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	getyx(stdscr, row, col);
	if (move(screen_player_row + 1, 0) == OK) {
		bkgdset(screen_objects[SCREEN_OBJ_PLAYER].attr);
		screen_vprintf(fmt, ap);
		(void)move(row, col);
		(void)refresh();
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	va_end(ap);
}

void
screen_player_track_print(const char *s)
{
	int col, row;

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	getyx(stdscr, row, col);
	if (move(screen_player_row, 0) == OK) {
		bkgdset(screen_objects[SCREEN_OBJ_PLAYER].attr);
		screen_print_row(s);
		(void)move(row, col);
		(void)refresh();
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_print(void)
{
	view_print();
	player_print();
	if (prompt_is_active())
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
	int col, row;

	(void)addnstr(s, COLS);

	if (strlen(s) < (size_t)COLS)
		(void)clrtoeol();
	else {
		/*
		 * If the length of the printed string is equal to the screen
		 * width, the cursor will advance to the next row. Undo this by
		 * moving the cursor back to the original row.
		 */
		getyx(stdscr, row, col);
		(void)move(row - 1, COLS - 1);
	}
}

void
screen_prompt_begin(void)
{
	screen_show_cursor(1);
}

void
screen_prompt_end(void)
{
	screen_show_cursor(0);
	screen_status_clear();

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	(void)move(screen_view_cursor_row, 0);
	(void)refresh();
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_prompt_printf(size_t cursorpos, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	if (move(screen_status_row, 0) == OK) {
		bkgdset(screen_objects[SCREEN_OBJ_PROMPT].attr);
		screen_vprintf(fmt, ap);
		(void)move(screen_status_row, (int)cursorpos);
		(void)refresh();
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	va_end(ap);
}

void
screen_refresh(void)
{
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	(void)clear();
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	screen_resize();
	screen_print();
}

static void
screen_resize(void)
{
#if defined(HAVE_RESIZETERM) && defined(TIOCGWINSZ)
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
#ifdef HAVE_IS_TERM_RESIZED
	if (is_term_resized(ws.ws_row, ws.ws_col) == FALSE) {
		XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
		return;
	}
#endif

	if (resizeterm(ws.ws_row, ws.ws_col) == ERR)
		/*
		 * resizeterm() might have failed to allocate memory, so treat
		 * this as fatal.
		 */
		LOG_FATALX("resizeterm() failed");
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
#else
	/*
	 * This method is described in
	 * <http://invisible-island.net/ncurses/ncurses.faq.html#
	 * handle_resize>. It "relies on side-effects of the library functions,
	 * and is moderately portable".
	 */
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	(void)endwin();
	(void)refresh();
	/* Re-enable keypad. */
	(void)keypad(stdscr, TRUE);
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	screen_configure_cursor();
#endif

	screen_calculate_rows();
}

static void
screen_show_cursor(int show)
{
	if (show) {
		XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
		(void)curs_set(1);
		XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	} else if (!option_get_boolean("show-cursor")) {
		XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
		(void)curs_set(0);
		XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	}
}

#ifdef SIGWINCH
/* ARGSUSED */
static void
screen_sigwinch_handler(UNUSED int sig)
{
	screen_sigwinch = 1;
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
		(void)clrtoeol();
		(void)move(row, col);
		(void)refresh();
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

unsigned int
screen_view_get_nrows(void)
{
	return (unsigned int)screen_view_nrows;
}

void
screen_view_move_cursor(unsigned int row)
{
	if ((int)row < screen_view_nrows) {
		XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
		(void)move(SCREEN_VIEW_ROW + (int)row, 0);
		XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	}
}

void
screen_view_print(const char *s)
{
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	bkgdset(screen_objects[SCREEN_OBJ_VIEW].attr);
	screen_print_row(s);
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_view_print_active(const char *s)
{
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	bkgdset(screen_objects[SCREEN_OBJ_ACTIVE].attr);
	screen_print_row(s);
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_view_print_begin(void)
{
	int i;

	/* Clear the view area. */
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	bkgdset(screen_objects[SCREEN_OBJ_VIEW].attr);
	for (i = 0; i < screen_view_nrows; i++) {
		(void)move(SCREEN_VIEW_ROW + i, 0);
		(void)clrtoeol();
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_view_print_end(void)
{
	int col;

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	(void)refresh();
	getyx(stdscr, screen_view_cursor_row, col);
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
}

void
screen_view_print_selected(const char *s)
{
	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	bkgdset(screen_objects[SCREEN_OBJ_SELECTOR].attr);
	screen_print_row(s);
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
		/* No refresh() yet. */
	}
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);
	va_end(ap);
}

void
screen_view_title_printf_right(const char *fmt, ...)
{
	va_list	 ap;
	int	 len;
	char	*buf;

	va_start(ap, fmt);
	len = xvasprintf(&buf, fmt, ap);
	va_end(ap);

	XPTHREAD_MUTEX_LOCK(&screen_curses_mtx);
	if (len < COLS)
		(void)mvaddstr(SCREEN_TITLE_ROW, COLS - len, buf);
	else
		(void)mvaddstr(SCREEN_TITLE_ROW, 0, buf + len - COLS);
	(void)refresh();
	XPTHREAD_MUTEX_UNLOCK(&screen_curses_mtx);

	free(buf);
}

/*
 * The screen_curses_mtx mutex must be locked before calling this function.
 */
static void
screen_vprintf(const char *fmt, va_list ap)
{
	int	 col, len, row;
	char	*buf;

	len = xvasprintf(&buf, fmt, ap);
	(void)addnstr(buf, COLS);
	free(buf);

	if (len < COLS)
		(void)clrtoeol();
	else {
		/*
		 * If the length of the printed string is equal to the screen
		 * width, the cursor will advance to the next row. Undo this by
		 * moving the cursor back to the original row.
		 */
		getyx(stdscr, row, col);
		(void)move(row - 1, COLS - 1);
	}
}
