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

#include <pthread.h>

#include "siren.h"

static enum input_mode	input_mode = INPUT_MODE_VIEW;
static pthread_mutex_t	input_mode_mtx = PTHREAD_MUTEX_INITIALIZER;

static int		input_quit;

void
input_end(void)
{
	input_quit = 1;
}

enum input_mode
input_get_mode(void)
{
	enum input_mode mode;

	XPTHREAD_MUTEX_LOCK(&input_mode_mtx);
	mode = input_mode;
	XPTHREAD_MUTEX_UNLOCK(&input_mode_mtx);
	return mode;
}

void
input_handle_key(void)
{
	while (!input_quit)
		view_handle_key(screen_get_key());
}

void
input_set_mode(enum input_mode mode)
{
	XPTHREAD_MUTEX_LOCK(&input_mode_mtx);
	input_mode = mode;
	XPTHREAD_MUTEX_UNLOCK(&input_mode_mtx);
}
