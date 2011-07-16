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
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "siren.h"

#define PROMPT_LINESIZE 1024

static void		 prompt_handle_input(struct history *) NONNULL();
static void		 prompt_mode_begin(const char *) NONNULL();
static void		 prompt_mode_end(void);

static struct history	*prompt_command_history;
static struct history	*prompt_search_history;
static pthread_mutex_t	 prompt_active_mtx = PTHREAD_MUTEX_INITIALIZER;
static int		 prompt_active;

static size_t		 prompt_linelen;
static size_t		 prompt_linepos;
static size_t		 prompt_linesize;
static size_t		 prompt_scroll_offset;
static char		*prompt_line;

static const char	*prompt_prompt;
static size_t		 prompt_promptlen;

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
prompt_clear_command_history(void)
{
	history_clear(prompt_command_history);
}

void
prompt_clear_search_history(void)
{
	history_clear(prompt_search_history);
}

void
prompt_end(void)
{
	history_free(prompt_command_history);
	history_free(prompt_search_history);
}

int
prompt_get_answer(const char *question)
{
	int	 answer;
	char	*prompt;

	(void)xasprintf(&prompt, "%s? ([y]/n): ", question);
	prompt_mode_begin(prompt);
	prompt_print();

	answer = -1;
	do
		switch (screen_get_key()) {
		case K_CTRL('G'):
		case 'N':
		case 'n':
			answer = 0;
			break;
		case 'Y':
		case 'y':
		case K_ENTER:
			answer = 1;
			break;
		}
	while (answer == -1);

	prompt_mode_end();
	free(prompt_line);
	free(prompt);
	return answer;
}

char *
prompt_get_command(const char *prompt)
{
	prompt_mode_begin(prompt);
	prompt_handle_input(prompt_command_history);
	prompt_mode_end();
	return prompt_line;
}

char *
prompt_get_search(const char *prompt)
{
	prompt_mode_begin(prompt);
	prompt_handle_input(prompt_search_history);
	prompt_mode_end();
	return prompt_line;
}

void
prompt_handle_input(struct history *history)
{
	size_t		 i, j;
	int		 done, key;
	const char	*line;

	history_rewind(history);

	done = 0;
	while (!done) {
		prompt_print();
		switch ((key = screen_get_key())) {
		case K_CTRL('A'):
		case K_HOME:
			prompt_linepos = 0;
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
		case K_CTRL('G'):
		case K_ESCAPE:
			free(prompt_line);
			prompt_line = NULL;
			done = 1;
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
			while (prompt_linepos - i > 0 &&
			    !isalnum((int)prompt_line[prompt_linepos - i - 1]))
				i++;
			while (prompt_linepos - i > 0 &&
			    isalnum((int)prompt_line[prompt_linepos - i - 1]))
				i++;

			prompt_linepos -= i;
			for (j = prompt_linepos; j < prompt_linelen; j++)
				prompt_line[j] = prompt_line[j + i];
			prompt_linelen -= i;
			break;
		case K_BACKSPACE:
			if (prompt_linepos == 0)
				break;

			prompt_linepos--;
			for (i = prompt_linepos; i < prompt_linelen; i++)
				prompt_line[i] = prompt_line[i + 1];
			prompt_linelen--;
			break;
		case K_DOWN:
			if ((line = history_get_prev(history)) == NULL) {
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
			if (prompt_linelen > 0)
				history_add(history, prompt_line);
			else {
				free(prompt_line);
				prompt_line = NULL;
			}
			done = 1;
			break;
		case K_LEFT:
			if (prompt_linepos > 0)
				prompt_linepos--;
			break;
		case K_RIGHT:
			if (prompt_linepos < prompt_linelen)
				prompt_linepos++;
			break;
		case K_UP:
			if ((line = history_get_next(history)) != NULL) {
				free(prompt_line);
				prompt_line = xstrdup(line);
				prompt_linelen = strlen(prompt_line);
				prompt_linesize = prompt_linelen + 1;
				prompt_linepos = prompt_linelen;
			}
			break;
		default:
			if (key < 32 || key > 126)
				break;

			prompt_linelen++;

			if (prompt_linelen == prompt_linesize) {
				prompt_linesize += PROMPT_LINESIZE;
				prompt_line = xrealloc(prompt_line,
				    prompt_linesize);
			}

			for (i = prompt_linelen; i > prompt_linepos; i--)
				prompt_line[i] = prompt_line[i - 1];

			prompt_line[prompt_linepos++] = (char)key;
			break;
		}
	}
}

void
prompt_init(void)
{
	prompt_command_history = history_init();
	prompt_search_history = history_init();
}

int
prompt_is_active(void)
{
	int active;

	XPTHREAD_MUTEX_LOCK(&prompt_active_mtx);
	active = prompt_active;
	XPTHREAD_MUTEX_UNLOCK(&prompt_active_mtx);
	return active;
}

static void
prompt_mode_begin(const char *prompt)
{
	prompt_prompt = prompt;
	prompt_promptlen = strlen(prompt_prompt);

	prompt_linelen = 0;
	prompt_linepos = 0;
	prompt_linesize = PROMPT_LINESIZE;
	prompt_line = xmalloc(prompt_linesize);
	prompt_line[0] = '\0';
	prompt_scroll_offset = 0;

	XPTHREAD_MUTEX_LOCK(&prompt_active_mtx);
	prompt_active = 1;
	XPTHREAD_MUTEX_UNLOCK(&prompt_active_mtx);

	screen_prompt_begin();
}

static void
prompt_mode_end(void)
{
	screen_prompt_end();
	XPTHREAD_MUTEX_LOCK(&prompt_active_mtx);
	prompt_active = 0;
	XPTHREAD_MUTEX_UNLOCK(&prompt_active_mtx);
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

void
prompt_resize_histories(void)
{
	unsigned int maxentries;

	maxentries = (unsigned int)option_get_number("max-history-entries");
	history_resize(prompt_command_history, maxentries);
	history_resize(prompt_search_history, maxentries);
}
