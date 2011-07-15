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
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include "siren.h"

#ifdef HAVE_FREEBSD_BSWAP16
#include <sys/endian.h>
#endif

#ifdef HAVE_NETBSD_BSWAP16
#include <sys/types.h>
#include <machine/bswap.h>
#endif

#ifdef HAVE_OPENBSD_SWAP16
#include <sys/types.h>
#endif

#ifdef HAVE_FREEBSD_BSWAP16
#define PLAYER_SWAP16(i)	bswap16((uint16_t)(i))
#elif defined(HAVE_NETBSD_BSWAP16)
#define PLAYER_SWAP16(i)	bswap16((u_int16_t)(i))
#elif defined(HAVE_OPENBSD_SWAP16)
#define PLAYER_SWAP16(i)	swap16((u_int16_t)(i))
#else
#define PLAYER_SWAP16(i)	((uint16_t)(i) >> 8 | (uint16_t)(i) << 8)
#endif

enum player_command {
	PLAYER_COMMAND_PAUSE,
	PLAYER_COMMAND_PLAY,
	PLAYER_COMMAND_QUIT,
	PLAYER_COMMAND_STOP
};

enum player_state {
	PLAYER_STATE_PAUSED,
	PLAYER_STATE_PLAYING,
	PLAYER_STATE_STOPPED
};

struct player_sample_buffer {
	int16_t			*samples;
	size_t			 maxsamples;
	size_t			 nsamples;
	int			 swap;
};

static void			 player_close_op(void);
static int			 player_open_op(void);
static void			*player_playback_handler(void *);
static void			 player_print_status(void);
static void			 player_print_track(void);
static void			 player_quit(void);
static void			 player_set_signal_mask(void);

static pthread_t		 player_playback_thd;

static enum player_state	 player_state = PLAYER_STATE_STOPPED;
static pthread_mutex_t		 player_state_mtx = PTHREAD_MUTEX_INITIALIZER;
static enum player_command	 player_command = PLAYER_COMMAND_STOP;
static pthread_cond_t		 player_command_cond =
				    PTHREAD_COND_INITIALIZER;

static const struct op		*player_op = NULL;
static int			 player_op_opened;
static pthread_mutex_t		 player_op_mtx = PTHREAD_MUTEX_INITIALIZER;

static struct track		*player_track = NULL;
static pthread_mutex_t		 player_track_mtx = PTHREAD_MUTEX_INITIALIZER;

static enum byte_order		 player_byte_order;

/*
 * The player_state_mtx mutex must be locked before calling this function.
 */
static int
player_begin_playback(struct player_sample_buffer *buf)
{
	int	 ret;
	char	*error;

	error = NULL;

	XPTHREAD_MUTEX_LOCK(&player_track_mtx);
	XPTHREAD_MUTEX_LOCK(&player_op_mtx);

	if (player_track == NULL)
		goto error;

	if ((ret = player_track->ip->open(player_track, &error))) {
		msg_ip_err(ret, error, "Cannot open track");
		free(error);
		goto error;
	}

	if (player_open_op() == -1) {
		player_track->ip->close(player_track);
		goto error;
	}

	if ((ret = player_op->start(&player_track->format))) {
		msg_op_err(player_op, ret, "Cannot start playback");
		player_track->ip->close(player_track);
		goto error;
	}

	buf->maxsamples = player_op->get_buffer_size() / 2;
	buf->samples = xcalloc(buf->maxsamples, sizeof *buf->samples);
	buf->swap = player_track->format.byte_order != player_byte_order;

	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);
	return 0;

error:
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);
	return -1;
}

void
player_change_op(void)
{
	const struct op	*op;
	int		 ret;
	char		*name;

	name = option_get_string("output-plugin");
	if ((op = plugin_find_op(name)) == NULL)
		msg_errx("Output plug-in not found: %s", name);
	else {
		player_stop();
		player_close_op();
		LOG_INFO("opening %s output plug-in", op->name);
		if ((ret = op->open()))
			msg_op_err(op, ret, "Cannot open %s output plug-in",
			    op->name);
		else {
			XPTHREAD_MUTEX_LOCK(&player_op_mtx);
			player_op = op;
			player_op_opened = 1;
			XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);
		}
	}
	free(name);
}

static void
player_close_op(void)
{
	XPTHREAD_MUTEX_LOCK(&player_op_mtx);
	if (player_op_opened) {
		player_op->close();
		player_op_opened = 0;
	}
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);
}

static void
player_determine_byte_order(void)
{
	int i;

	i = 1;
	if (*(char *)&i == 1)
		player_byte_order = BYTE_ORDER_LITTLE;
	else
		player_byte_order = BYTE_ORDER_BIG;
}

void
player_end(void)
{
	player_quit();
	XPTHREAD_JOIN(player_playback_thd, NULL);
	player_close_op();
	track_free(player_track);
}

static void
player_end_playback(struct player_sample_buffer *buf)
{
	int ret;

	XPTHREAD_MUTEX_LOCK(&player_track_mtx);
	player_track->ip->close(player_track);
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);

	XPTHREAD_MUTEX_LOCK(&player_op_mtx);
	if ((ret = player_op->stop())) {
		msg_op_err(player_op, ret, "Cannot stop playback");
		player_close_op();
	}
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);

	free(buf->samples);
}

enum byte_order
player_get_byte_order(void)
{
	return player_byte_order;
}

static int
player_get_track(void)
{
	struct track *t;

	if (!option_get_boolean("continue"))
		return -1;

	if ((t = queue_get_next_track()) == NULL &&
	    (t = library_get_next_track()) == NULL)
		return -1;

	XPTHREAD_MUTEX_LOCK(&player_track_mtx);
	track_free(player_track);
	player_track = t;
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);
	return 0;
}

void
player_init(void)
{
	player_determine_byte_order();
	XPTHREAD_CREATE(&player_playback_thd, NULL, player_playback_handler,
	    NULL);
}

/*
 * The player_op_mtx mutex must be locked before calling this function.
 */
static int
player_open_op(void)
{
	int	 ret;
	char	*name;

	if (!player_op_opened) {
		if (player_op == NULL) {
			name = option_get_string("output-plugin");
			if ((player_op = plugin_find_op(name)) == NULL) {
				msg_errx("Output plug-in not found: %s", name);
				free(name);
				return -1;
			}
			free(name);
		}

		LOG_INFO("opening %s output plug-in", player_op->name);
		if ((ret = player_op->open())) {
			msg_op_err(player_op, ret,
			    "Cannot open %s output plug-in", player_op->name);
			return -1;
		}

		player_op_opened = 1;
	}

	return 0;
}

void
player_pause(void)
{
	XPTHREAD_MUTEX_LOCK(&player_state_mtx);
	if (player_state == PLAYER_STATE_PLAYING)
		player_command = PLAYER_COMMAND_PAUSE;
	else if (player_state == PLAYER_STATE_PAUSED) {
		player_command = PLAYER_COMMAND_PLAY;
		XPTHREAD_COND_BROADCAST(&player_command_cond);
	}
	XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);
}

void
player_play(void)
{
	XPTHREAD_MUTEX_LOCK(&player_state_mtx);
	if (player_state == PLAYER_STATE_PLAYING) {
		player_command = PLAYER_COMMAND_STOP;
		XPTHREAD_COND_WAIT(&player_command_cond, &player_state_mtx);
	}

	player_command = PLAYER_COMMAND_PLAY;
	XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);
	XPTHREAD_COND_BROADCAST(&player_command_cond);
}

void
player_play_next(void)
{
	struct track *t;

	if ((t = library_get_next_track()) != NULL)
		player_play_track(t);
}

void
player_play_prev(void)
{
	struct track *t;

	if ((t = library_get_prev_track()) != NULL)
		player_play_track(t);
}

static int
player_play_sample_buffer(struct player_sample_buffer *buf)
{
	size_t	 i;
	int	 ret;
	char	*error;

	error = NULL;

	XPTHREAD_MUTEX_LOCK(&player_track_mtx);
	ret = player_track->ip->read(player_track, buf->samples,
	    buf->maxsamples, &error);
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);

	if (ret == 0)
		/* EOF reached. */
		return -1;

	if (ret < 0) {
		/* Error encountered. */
		msg_ip_err(ret, error, "Cannot read from track");
		free(error);
		return -1;
	}

	buf->nsamples = ret;

	if (buf->swap)
		for (i = 0; i < buf->nsamples; i++)
			buf->samples[i] = PLAYER_SWAP16(buf->samples[i]);

	XPTHREAD_MUTEX_LOCK(&player_op_mtx);
	ret = player_op->write(buf->samples, buf->nsamples * 2);
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);

	if (ret == -1) {
		msg_errx("Cannot play back");
		return -1;
	}

	return 0;
}

void
player_play_track(struct track *t)
{
	player_stop();
	XPTHREAD_MUTEX_LOCK(&player_track_mtx);
	track_free(player_track);
	player_track = t;
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);
	player_play();
}

/* ARGSUSED */
static void *
player_playback_handler(UNUSED void *p)
{
	struct player_sample_buffer buf;

#ifdef SIGWINCH
	/*
	 * Block SIGWINCH in this thread so that it can be handled in the main
	 * thread by screen_get_key().
	 */
	player_set_signal_mask();
#endif

	XPTHREAD_MUTEX_LOCK(&player_state_mtx);
	for (;;) {
		if (player_command == PLAYER_COMMAND_PLAY &&
		    player_get_track() == -1)
			player_command = PLAYER_COMMAND_STOP;

		if (player_command == PLAYER_COMMAND_STOP) {
			XPTHREAD_COND_WAIT(&player_command_cond,
			    &player_state_mtx);

			if (player_command == PLAYER_COMMAND_QUIT)
				break;
		}

		player_print_track();

		if (player_begin_playback(&buf) == -1)
			continue;

		player_state = PLAYER_STATE_PLAYING;
		XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);

		for (;;) {
			if (player_play_sample_buffer(&buf) == -1) {
				XPTHREAD_MUTEX_LOCK(&player_state_mtx);
				break;
			}

			XPTHREAD_MUTEX_LOCK(&player_state_mtx);
			if (player_command == PLAYER_COMMAND_PAUSE) {
				player_state = PLAYER_STATE_PAUSED;
				player_print_status();

				XPTHREAD_COND_WAIT(&player_command_cond,
				    &player_state_mtx);

				if (player_command == PLAYER_COMMAND_PLAY)
					player_state = PLAYER_STATE_PLAYING;
			}

			if (player_command == PLAYER_COMMAND_STOP)
				break;

			player_print_status();
			XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);
		}

		player_end_playback(&buf);
		player_state = PLAYER_STATE_STOPPED;
		player_print_status();

		if (player_command == PLAYER_COMMAND_STOP)
			XPTHREAD_COND_BROADCAST(&player_command_cond);
	}
	XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);

	return NULL;
}

void
player_print(void)
{
	XPTHREAD_MUTEX_LOCK(&player_state_mtx);
	player_print_track();
	player_print_status();
	XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);
}

/*
 * The player_state_mtx mutex must be locked before calling this function.
 */
static void
player_print_status(void)
{
	unsigned int	 duration, position;
	int		 ret, volume;
	char		*error;
	const char	*mode, *state;

	error = NULL;

	XPTHREAD_MUTEX_LOCK(&player_track_mtx);
	switch (player_state) {
	case PLAYER_STATE_PAUSED:
		state = "Paused";
		ret = player_track->ip->get_position(player_track, &position,
		    &error);
		duration = player_track->duration;
		break;
	case PLAYER_STATE_PLAYING:
		state = "Playing";
		ret = player_track->ip->get_position(player_track, &position,
		    &error);
		duration = player_track->duration;
		break;
	case PLAYER_STATE_STOPPED:
	default:
		state = "Stopped";
		position = 0;
		duration = player_track == NULL ? 0 : player_track->duration;
		ret = 0;
		break;
	}
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);

	if (ret < 0) {
		msg_ip_err(ret , error, "Cannot get current position");
		free(error);
	}

	XPTHREAD_MUTEX_LOCK(&player_op_mtx);
	if (player_open_op() == -1 || !player_op->get_volume_support())
		volume = 0;
	else if ((volume = player_op->get_volume()) < 0) {
		msg_op_err(player_op, volume, "Cannot get volume");
		volume = 0;
	}
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);

	if (option_get_boolean("continue"))
		mode = "continue";
	else
		mode = "";

	screen_player_status_printf("%-7s  %d:%02d / %u:%02u  %d%%  %s", state,
	    MINS(position), MSECS(position), MINS(duration), MSECS(duration),
	    volume, mode);
}

/*
 * The player_state_mtx mutex must be locked before calling this function.
 */
static void
player_print_track(void)
{
	size_t	 bufsize;
	char	*buf;
	char	*fmt;

	bufsize = screen_get_ncols() + 1;
	buf = xmalloc(bufsize);
	buf[0] = '\0';

	if (player_track != NULL) {
		fmt = option_get_string("player-track-format");
		(void)track_snprintf(buf, bufsize, fmt, player_track);
		free(fmt);
	}

	screen_player_track_print(buf);
	free(buf);
}

static void
player_quit(void)
{
	player_stop();
	XPTHREAD_MUTEX_LOCK(&player_state_mtx);
	player_command = PLAYER_COMMAND_QUIT;
	XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);
	XPTHREAD_COND_BROADCAST(&player_command_cond);
}

void
player_seek(int pos, int relative)
{
	unsigned int	 curpos;
	int		 ret;
	char		*error;

	XPTHREAD_MUTEX_LOCK(&player_state_mtx);
	XPTHREAD_MUTEX_LOCK(&player_track_mtx);

	if (player_state == PLAYER_STATE_STOPPED)
		goto out;

	error = NULL;

	if (relative) {
		if ((ret = player_track->ip->get_position(player_track,
		    &curpos, &error))) {
			msg_ip_err(ret, error, "Cannot get current position");
			free(error);
			goto out;
		}

		pos += curpos;
	}

	if (pos < 0)
		pos = 0;
	else if ((unsigned int)pos > player_track->duration)
		pos = player_track->duration;

	if ((ret = player_track->ip->seek(player_track, pos, &error))) {
		msg_ip_err(ret, error, "Cannot seek");
		free(error);
	}

out:
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);
	player_print_status();
	XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);
}

#ifdef SIGWINCH
static void
player_set_signal_mask(void)
{
	sigset_t ss;

	(void)sigemptyset(&ss);
	(void)sigaddset(&ss, SIGWINCH);
	(void)pthread_sigmask(SIG_BLOCK, &ss, NULL);
}
#endif

void
player_set_volume(int volume, int relative)
{
	int ret;

	XPTHREAD_MUTEX_LOCK(&player_op_mtx);

	if (player_open_op() == -1)
		goto out;

	if (!player_op->get_volume_support()) {
		msg_errx("Output plug-in does not have volume support");
		goto out;
	}

	if (relative) {
		if ((ret = player_op->get_volume()) < 0) {
			msg_op_err(player_op, ret, "Cannot get volume");
			goto out;
		}

		volume += ret;
		if (volume < 0)
			volume = 0;
		else if (volume > 100)
			volume = 100;
	}

	if ((ret = player_op->set_volume(volume)))
		msg_op_err(player_op, ret, "Cannot set volume");

out:
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);
	player_print_status();
}

void
player_stop(void)
{
	XPTHREAD_MUTEX_LOCK(&player_state_mtx);
	if (player_state != PLAYER_STATE_STOPPED) {
		player_command = PLAYER_COMMAND_STOP;
		if (player_state == PLAYER_STATE_PAUSED)
			XPTHREAD_COND_BROADCAST(&player_command_cond);
		XPTHREAD_COND_WAIT(&player_command_cond, &player_state_mtx);
	}
	XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);
}
