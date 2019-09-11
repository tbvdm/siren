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

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include "siren.h"

static void			input_handle_signal(int);

static enum input_mode		input_mode = INPUT_MODE_VIEW;
static pthread_mutex_t		input_mode_mtx = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t	input_quit;
#ifdef SIGWINCH
static volatile sig_atomic_t	input_sigwinch;
#endif

void
input_end(void)
{
	input_quit = 1;
}

void
input_init(void)
{
	struct sigaction	sa;
#ifdef VDSUSP
	struct termios		tio;
#endif

	sa.sa_handler = input_handle_signal;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGINT, &sa, NULL) == -1)
		LOG_ERR("sigaction");
	if (sigaction(SIGQUIT, &sa, NULL) == -1)
		LOG_ERR("sigaction");
	if (sigaction(SIGTERM, &sa, NULL) == -1)
		LOG_ERR("sigaction");
#ifdef SIGWINCH
	if (sigaction(SIGWINCH, &sa, NULL) == -1)
		LOG_ERR("sigaction");
#endif

#ifdef VDSUSP
	/*
	 * Check if the DSUSP special character is set to ^Y. If it is, disable
	 * it so that ^Y becomes an ordinary character that can be bound to a
	 * command.
	 */
	if (tcgetattr(STDIN_FILENO, &tio) == -1)
		LOG_ERR("tcgetattr");
	else
		if (tio.c_cc[VDSUSP] == K_CTRL('Y')) {
			tio.c_cc[VDSUSP] = _POSIX_VDISABLE;
			if (tcsetattr(STDIN_FILENO, TCSANOW, &tio) == -1)
				LOG_ERR("tcsetattr");
		}
#endif
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
	struct pollfd	pfd[1];
	int		key;

	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN;

	while (!input_quit) {
#ifdef SIGWINCH
		if (input_sigwinch) {
			input_sigwinch = 0;
			screen_refresh();
		}
#endif

		if (poll(pfd, NELEMENTS(pfd), -1) == -1) {
			if (errno != EINTR)
				LOG_FATAL("poll");
		} else {
			if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL))
				LOG_FATALX("poll() failed");

			key = screen_get_key();
			if (input_mode == INPUT_MODE_VIEW)
				view_handle_key(key);
			else
				prompt_handle_key(key);
		}
	}
}

static void
input_handle_signal(int sig)
{
	switch (sig) {
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		input_quit = 1;
		break;
#ifdef SIGWINCH
	case SIGWINCH:
		input_sigwinch = 1;
		break;
#endif
	}
}

void
input_set_mode(enum input_mode mode)
{
	XPTHREAD_MUTEX_LOCK(&input_mode_mtx);
	input_mode = mode;
	XPTHREAD_MUTEX_UNLOCK(&input_mode_mtx);
}
