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
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "siren.h"

#define PROMPT_LINESIZE 1024

enum prompt_mode {
	PROMPT_MODE_CHAR,
	PROMPT_MODE_LINE
};

static void		 prompt_line_handle_key(int);
static void		 prompt_mode_begin(enum prompt_mode, const char *,
			    struct history *, void (*)(char *, void *), void *)
			    NONNULL(2, 4);
static void		 prompt_mode_end(void);

enum prompt_mode	 prompt_mode;

static struct history	*prompt_command_history;
static struct history	*prompt_history;
static struct history	*prompt_search_history;

static size_t		 prompt_linelen;
static size_t		 prompt_linepos;
static size_t		 prompt_linesize;
static size_t		 prompt_scroll_offset;
static char		*prompt_line;

static const char	*prompt_prompt;
static size_t		 prompt_promptlen;

void			 (*prompt_callback)(char *, void *);
void			*prompt_callback_data;

static void
prompt_adjust_scroll_offset(void)
{
	size_t line_ncols, screen_ncols;

	screen_ncols = screen_get_ncols();
	if (prompt_promptlen < screen_ncols)
		line_ncols = screen_ncols - prompt_promptlen;
	else
		line_ncols = 0;

	/* Scroll left if necessary. */
	if (prompt_linepos < prompt_scroll_offset || line_ncols == 0)
		prompt_scroll_offset = prompt_linepos;

	/* Scroll right if necessary. */
	else if (prompt_linepos >= prompt_scroll_offset + line_ncols)
		prompt_scroll_offset = prompt_linepos - line_ncols + 1;

	/*
	 * If there is unused space at the end of the line, then use it if
	 * possible.
	 */
	else if (prompt_scroll_offset &&
	    prompt_scroll_offset + line_ncols > prompt_linelen) {
		if (prompt_linelen <= line_ncols)
			prompt_scroll_offset = 0;
		else
			prompt_scroll_offset = prompt_linelen - line_ncols + 1;
	}
}

void
prompt_end(void)
{
	history_free(prompt_command_history);
	history_free(prompt_search_history);

	/*
	 * It is possible that we are quitting while still in prompt mode (for
	 * example, because we received SIGTERM in prompt mode). In that case,
	 * we have to free the prompt input buffer.
	 */
	if (input_get_mode() == INPUT_MODE_PROMPT)
		free(prompt_line);
}

void
prompt_get_answer(const char *prompt, void (*callback)(char *, void *),
    void *callback_data)
{
	prompt_mode_begin(PROMPT_MODE_CHAR, prompt, NULL, callback,
	    callback_data);
}

void
prompt_get_command(const char *prompt, void (*callback)(char *, void *),
    void *callback_data)
{
	prompt_mode_begin(PROMPT_MODE_LINE, prompt, prompt_command_history,
	    callback, callback_data);
}

void
prompt_get_search_query(const char *prompt, void (*callback)(char *, void *),
    void *callback_data)
{
	prompt_mode_begin(PROMPT_MODE_LINE, prompt, prompt_search_history,
	    callback, callback_data);
}

static void
prompt_char_handle_key(int key)
{
	switch (key) {
	case 'N':
	case 'n':
	case K_CTRL('G'):
		prompt_line[0] = 'n';
		prompt_mode_end();
		break;
	case 'Y':
	case 'y':
	case K_ENTER:
		prompt_line[0] = 'y';
		prompt_mode_end();
		break;
	}
}

void
prompt_handle_key(int key)
{
	if (prompt_mode == PROMPT_MODE_CHAR)
		prompt_char_handle_key(key);
	else
		prompt_line_handle_key(key);
}

static void
prompt_line_handle_key(int key)
{
	size_t		 i, j;
	int		 done;
	const char	*line;

	done = 0;
	switch (key) {
	case K_CTRL('A'):
	case K_HOME:
		prompt_linepos = 0;
		break;
	case K_CTRL('B'):
	case K_LEFT:
		if (prompt_linepos > 0)
			prompt_linepos--;
		break;
	case K_CTRL('D'):
	case K_DELETE:
		if (prompt_linepos == prompt_linelen)
			break;

		for (i = prompt_linepos; i < prompt_linelen; i++)
			prompt_line[i] = prompt_line[i + 1];
		prompt_linelen--;
		break;
	case K_CTRL('E'):
	case K_END:
		prompt_linepos = prompt_linelen;
		break;
	case K_CTRL('F'):
	case K_RIGHT:
		if (prompt_linepos < prompt_linelen)
			prompt_linepos++;
		break;
	case K_CTRL('G'):
	case K_ESCAPE:
		free(prompt_line);
		prompt_line = NULL;
		done = 1;
		break;
	case K_CTRL('H'):
	case K_BACKSPACE:
		if (prompt_linepos == 0)
			break;

		prompt_linepos--;
		for (i = prompt_linepos; i < prompt_linelen; i++)
			prompt_line[i] = prompt_line[i + 1];
		prompt_linelen--;
		break;
	case K_CTRL('K'):
		prompt_linelen = prompt_linepos;
		prompt_line[prompt_linelen] = '\0';
		break;
	case K_CTRL('U'):
		prompt_linelen = 0;
		prompt_linepos = 0;
		prompt_scroll_offset = 0;
		prompt_line[0] = '\0';
		break;
	case K_CTRL('W'):
		i = 0;
		while (i < prompt_linepos && !isalnum(
		    (unsigned char)prompt_line[prompt_linepos - i - 1]))
			i++;

		while (i < prompt_linepos && isalnum(
		    (unsigned char)prompt_line[prompt_linepos - i - 1]))
			i++;

		prompt_linepos -= i;
		prompt_linelen -= i;
		for (j = prompt_linepos; j <= prompt_linelen; j++)
			prompt_line[j] = prompt_line[j + i];
		break;
	case K_DOWN:
		if (prompt_history == NULL)
			break;

		if ((line = history_get_prev(prompt_history)) == NULL) {
			prompt_linelen = 0;
			prompt_linepos = 0;
			prompt_scroll_offset = 0;
			prompt_line[0] = '\0';
		} else {
			free(prompt_line);
			prompt_line = xstrdup(line);
			prompt_linelen = strlen(prompt_line);
			prompt_linesize = prompt_linelen + 1;
			prompt_linepos = prompt_linelen;
		}
		break;
	case K_ENTER:
		if (prompt_history != NULL && prompt_linelen > 0)
			history_add(prompt_history, prompt_line);
		else {
			free(prompt_line);
			prompt_line = NULL;
		}
		done = 1;
		break;
	case K_UP:
		if (prompt_history == NULL)
			break;

		if ((line = history_get_next(prompt_history)) != NULL) {
			free(prompt_line);
			prompt_line = xstrdup(line);
			prompt_linelen = strlen(prompt_line);
			prompt_linesize = prompt_linelen + 1;
			prompt_linepos = prompt_linelen;
		}
		break;
	default:
		/*
		 * Ignore control characters and function keys not handled
		 * above.
		 */
		if (iscntrl((unsigned char)key) || key > 127)
			break;

		if (++prompt_linelen == prompt_linesize) {
			prompt_linesize += PROMPT_LINESIZE;
			prompt_line = xrealloc(prompt_line, prompt_linesize);
		}

		for (i = prompt_linelen; i > prompt_linepos; i--)
			prompt_line[i] = prompt_line[i - 1];

		prompt_line[prompt_linepos++] = (char)key;
		break;
	}

	if (done)
		prompt_mode_end();
	else
		prompt_print();
}

void
prompt_init(void)
{
	prompt_command_history = history_init();
	prompt_search_history = history_init();
}

static void
prompt_mode_begin(enum prompt_mode mode, const char *prompt,
    struct history *history, void (*callback)(char *, void *),
    void *callback_data)
{
	prompt_history = history;
	if (prompt_history != NULL)
		history_rewind(prompt_history);

	prompt_mode = mode;
	if (prompt_mode == PROMPT_MODE_CHAR)
		prompt_linesize = 1;
	else
		prompt_linesize = PROMPT_LINESIZE;

	prompt_prompt = prompt;
	prompt_promptlen = strlen(prompt_prompt);
	prompt_callback = callback;
	prompt_callback_data = callback_data;
	prompt_line = xmalloc(prompt_linesize);
	prompt_linelen = 0;
	prompt_linepos = 0;
	prompt_line[0] = '\0';
	prompt_scroll_offset = 0;

	input_set_mode(INPUT_MODE_PROMPT);
	screen_prompt_begin();
	prompt_print();
}

static void
prompt_mode_end(void)
{
	screen_prompt_end();
	input_set_mode(INPUT_MODE_VIEW);
	prompt_callback(prompt_line, prompt_callback_data);
}

void
prompt_print(void)
{
	size_t cursorpos;

	prompt_adjust_scroll_offset();
	cursorpos = prompt_promptlen + prompt_linepos - prompt_scroll_offset;

	screen_prompt_printf(cursorpos, "%s%s", prompt_prompt,
	    prompt_line + prompt_scroll_offset);
}
