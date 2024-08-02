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

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include "siren.h"

#define PLAYER_FMT_CONTINUE	0
#define PLAYER_FMT_DURATION	1
#define PLAYER_FMT_POSITION	2
#define PLAYER_FMT_REPEAT_ALL	3
#define PLAYER_FMT_REPEAT_TRACK	4
#define PLAYER_FMT_SOURCE	5
#define PLAYER_FMT_STATE	6
#define PLAYER_FMT_VOLUME	7
#define PLAYER_FMT_NVARS	8

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

static pthread_mutex_t		 player_source_mtx = PTHREAD_MUTEX_INITIALIZER;
static enum player_source	 player_source = PLAYER_SOURCE_LIBRARY;

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
player_begin_playback(struct sample_buffer *sb)
{
	XPTHREAD_MUTEX_LOCK(&player_track_mtx);
	XPTHREAD_MUTEX_LOCK(&player_op_mtx);

	if (player_track == NULL)
		goto error1;

	if (player_track->ip == NULL) {
		msg_errx("%s: Unsupported file format", player_track->path);
		goto error1;
	}

	if (player_track->ip->open(player_track))
		goto error1;

	LOG_DEBUG("rate=%u, nchannels=%u, nbits=%u", player_track->format.rate,
	    player_track->format.nchannels, player_track->format.nbits);

	if (player_open_op() == -1)
		goto error2;

	if (player_op->start(&player_track->format) == -1)
		goto error2;

	if (player_track->format.nbits <= 8)
		sb->nbytes = 1;
	else if (player_track->format.nbits <= 16)
		sb->nbytes = 2;
	else
		sb->nbytes = 4;

	sb->size_b = player_op->get_buffer_size();
	sb->size_s = sb->size_b / sb->nbytes;

	if (sb->size_s == 0) {
		msg_errx("Output buffer too small");
		goto error2;
	}

	sb->data = xmalloc(sb->size_b);
	sb->data1 = sb->data;
	sb->data2 = sb->data;
	sb->data4 = sb->data;

	if (player_track->format.byte_order == player_byte_order ||
	    sb->nbytes == 1)
		sb->swap = 0;
	else
		sb->swap = 1;

	LOG_DEBUG("size_b=%zu, size_s=%zu, nbytes=%u, swap=%d", sb->size_b,
	    sb->size_s, sb->nbytes, sb->swap);

	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);
	return 0;

error2:
	player_track->ip->close(player_track);
error1:
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);
	return -1;
}

void
player_change_op(void)
{
	player_stop();

	XPTHREAD_MUTEX_LOCK(&player_op_mtx);
	player_close_op();
	player_op = NULL;
	player_open_op();
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);

	XPTHREAD_MUTEX_LOCK(&player_state_mtx);
	player_print_status();
	XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);
}

/*
 * The player_op_mtx mutex must be locked before calling this function.
 */
static void
player_close_op(void)
{
	if (player_op_opened) {
		player_op->close();
		player_op_opened = 0;
	}
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
}

static void
player_end_playback(struct sample_buffer *sb)
{
	XPTHREAD_MUTEX_LOCK(&player_track_mtx);
	player_track->ip->close(player_track);
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);

	XPTHREAD_MUTEX_LOCK(&player_op_mtx);
	if (player_op->stop() == -1)
		player_close_op();
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);

	free(sb->data);
}

void
player_forcibly_close_op(void)
{
	player_stop();

	XPTHREAD_MUTEX_LOCK(&player_op_mtx);
	if (player_op_opened) {
		LOG_INFO("forcibly closing %s", player_op->name);
		player_op->close();
		player_op_opened = 0;
	}
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);
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

	if (!option_get_boolean("repeat-track")) {
		if ((t = queue_get_next_track()) == NULL) {
			XPTHREAD_MUTEX_LOCK(&player_source_mtx);
			switch (player_source) {
			case PLAYER_SOURCE_BROWSER:
				t = browser_get_next_track();
				break;
			case PLAYER_SOURCE_LIBRARY:
				t = library_get_next_track();
				break;
			case PLAYER_SOURCE_PLAYLIST:
				t = playlist_get_next_track();
				break;
			}
			XPTHREAD_MUTEX_UNLOCK(&player_source_mtx);

			if (t == NULL)
				return -1;
		}

		XPTHREAD_MUTEX_LOCK(&player_track_mtx);
		player_track = t;
		XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);
	}

	return option_get_boolean("continue") ? 0 : -1;
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
	char *name;

	if (player_op_opened)
		/* Output plug-in already opened. */
		return 0;

	if (player_op == NULL) {
		name = option_get_string("output-plugin");
		if ((player_op = plugin_find_op(name)) == NULL) {
			msg_errx("Output plug-in not found: %s", name);
			free(name);
			return -1;
		}
		free(name);
	}

	LOG_INFO("opening %s", player_op->name);
	if (player_op->open() == -1)
		return -1;

	player_op_opened = 1;
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

	XPTHREAD_MUTEX_LOCK(&player_source_mtx);
	switch (player_source) {
	case PLAYER_SOURCE_BROWSER:
		t = browser_get_next_track();
		break;
	case PLAYER_SOURCE_LIBRARY:
		t = library_get_next_track();
		break;
	case PLAYER_SOURCE_PLAYLIST:
		t = playlist_get_next_track();
		break;
	default:
		t = NULL;
		break;
	}
	XPTHREAD_MUTEX_UNLOCK(&player_source_mtx);

	if (t != NULL)
		player_play_track(t);
}

void
player_play_prev(void)
{
	struct track *t;

	XPTHREAD_MUTEX_LOCK(&player_source_mtx);
	switch (player_source) {
	case PLAYER_SOURCE_BROWSER:
		t = browser_get_prev_track();
		break;
	case PLAYER_SOURCE_LIBRARY:
		t = library_get_prev_track();
		break;
	case PLAYER_SOURCE_PLAYLIST:
		t = playlist_get_prev_track();
		break;
	default:
		t = NULL;
		break;
	}
	XPTHREAD_MUTEX_UNLOCK(&player_source_mtx);

	if (t != NULL)
		player_play_track(t);
}

static int
player_play_sample_buffer(struct sample_buffer *sb)
{
	size_t	i;
	int	ret;

	XPTHREAD_MUTEX_LOCK(&player_track_mtx);
	ret = player_track->ip->read(player_track, sb);
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);

	if (ret == 0)
		/* EOF reached. */
		return -1;

	if (ret < 0)
		/* Error encountered. */
		goto error;

	if (sb->swap) {
		if (sb->nbytes == 2)
			for (i = 0; i < sb->len_s; i++)
				sb->data2[i] = swap16(sb->data2[i]);
		else
			for (i = 0; i < sb->len_s; i++)
				sb->data4[i] = swap32(sb->data4[i]);
	}

	XPTHREAD_MUTEX_LOCK(&player_op_mtx);
	ret = player_op->write(sb);
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);

	if (ret == -1)
		goto error;

	return 0;

error:
	if (!option_get_boolean("continue-after-error")) {
		XPTHREAD_MUTEX_LOCK(&player_state_mtx);
		player_command = PLAYER_COMMAND_STOP;
		XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);
	}
	return -1;
}

void
player_play_track(struct track *t)
{
	player_stop();
	XPTHREAD_MUTEX_LOCK(&player_track_mtx);
	player_track = t;
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);
	player_play();
}

static void *
player_playback_handler(UNUSED void *p)
{
	struct sample_buffer sb;

	/*
	 * Block all signals in this thread so that they can be handled in the
	 * main thread by input_handle_signal().
	 */
	player_set_signal_mask();

	XPTHREAD_MUTEX_LOCK(&player_state_mtx);
	for (;;) {
		if (player_command == PLAYER_COMMAND_PLAY) {
			if (player_get_track() == -1)
				player_command = PLAYER_COMMAND_STOP;
			player_print_track();
		}

		if (player_command == PLAYER_COMMAND_STOP) {
			XPTHREAD_COND_WAIT(&player_command_cond,
			    &player_state_mtx);
			if (player_command == PLAYER_COMMAND_QUIT)
				break;
			player_print_track();
		}

		if (player_begin_playback(&sb) == -1) {
			player_command = PLAYER_COMMAND_STOP;
			continue;
		}

		player_state = PLAYER_STATE_PLAYING;
		XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);

		for (;;) {
			if (player_play_sample_buffer(&sb) == -1) {
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

		player_end_playback(&sb);
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
	struct format		*format;
	struct format_variable	 vars[PLAYER_FMT_NVARS];
	unsigned int		 pos;
	int			 vol;

	vars[PLAYER_FMT_CONTINUE].lname = "continue";
	vars[PLAYER_FMT_CONTINUE].sname = 'c';
	vars[PLAYER_FMT_CONTINUE].type = FORMAT_VARIABLE_STRING;
	vars[PLAYER_FMT_DURATION].lname = "duration";
	vars[PLAYER_FMT_DURATION].sname = 'd';
	vars[PLAYER_FMT_DURATION].type = FORMAT_VARIABLE_TIME;
	vars[PLAYER_FMT_POSITION].lname = "position";
	vars[PLAYER_FMT_POSITION].sname = 'p';
	vars[PLAYER_FMT_POSITION].type = FORMAT_VARIABLE_TIME;
	vars[PLAYER_FMT_REPEAT_ALL].lname = "repeat-all";
	vars[PLAYER_FMT_REPEAT_ALL].sname = 'r';
	vars[PLAYER_FMT_REPEAT_ALL].type = FORMAT_VARIABLE_STRING;
	vars[PLAYER_FMT_REPEAT_TRACK].lname = "repeat-track";
	vars[PLAYER_FMT_REPEAT_TRACK].sname = 't';
	vars[PLAYER_FMT_REPEAT_TRACK].type = FORMAT_VARIABLE_STRING;
	vars[PLAYER_FMT_SOURCE].lname = "source";
	vars[PLAYER_FMT_SOURCE].sname = 'u';
	vars[PLAYER_FMT_SOURCE].type = FORMAT_VARIABLE_STRING;
	vars[PLAYER_FMT_STATE].lname = "state";
	vars[PLAYER_FMT_STATE].sname = 's';
	vars[PLAYER_FMT_STATE].type = FORMAT_VARIABLE_STRING;
	vars[PLAYER_FMT_VOLUME].lname = "volume";
	vars[PLAYER_FMT_VOLUME].sname = 'v';
	vars[PLAYER_FMT_VOLUME].type = FORMAT_VARIABLE_NUMBER;

	/* Set the state variable. */
	switch (player_state) {
	case PLAYER_STATE_PAUSED:
		vars[PLAYER_FMT_STATE].value.string = "Paused";
		break;
	case PLAYER_STATE_PLAYING:
		vars[PLAYER_FMT_STATE].value.string = "Playing";
		break;
	case PLAYER_STATE_STOPPED:
		vars[PLAYER_FMT_STATE].value.string = "Stopped";
		break;
	}

	XPTHREAD_MUTEX_LOCK(&player_track_mtx);

	/* Set the position variable. */
	if (player_state == PLAYER_STATE_STOPPED || player_track->ip == NULL ||
	    player_track->ip->get_position(player_track, &pos) == -1)
		vars[PLAYER_FMT_POSITION].value.time = 0;
	else
		vars[PLAYER_FMT_POSITION].value.time = pos;

	/* Set the duration variable. */
	if (player_track == NULL)
		vars[PLAYER_FMT_DURATION].value.time = 0;
	else {
		track_lock_metadata();
		vars[PLAYER_FMT_DURATION].value.time =
		    player_track->duration;
		track_unlock_metadata();
	}

	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);

	/* Set the volume variable. */
	XPTHREAD_MUTEX_LOCK(&player_op_mtx);
	if (player_open_op() == -1 || !player_op->get_volume_support() ||
	    (vol = player_op->get_volume()) == -1)
		vars[PLAYER_FMT_VOLUME].value.number = 0;
	else
		vars[PLAYER_FMT_VOLUME].value.number = vol;
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);

	/* Set the continue variable. */
	if (option_get_boolean("continue"))
		vars[PLAYER_FMT_CONTINUE].value.string = "continue";
	else
		vars[PLAYER_FMT_CONTINUE].value.string = "";

	/* Set the repeat-all variable. */
	if (option_get_boolean("repeat-all"))
		vars[PLAYER_FMT_REPEAT_ALL].value.string = "repeat-all";
	else
		vars[PLAYER_FMT_REPEAT_ALL].value.string = "";

	/* Set the repeat-track variable. */
	if (option_get_boolean("repeat-track"))
		vars[PLAYER_FMT_REPEAT_TRACK].value.string = "repeat-track";
	else
		vars[PLAYER_FMT_REPEAT_TRACK].value.string = "";

	/* Set the player-source variable. */
	XPTHREAD_MUTEX_LOCK(&player_source_mtx);
	switch (player_source) {
	case PLAYER_SOURCE_BROWSER:
		vars[PLAYER_FMT_SOURCE].value.string = "browser";
		break;
	case PLAYER_SOURCE_LIBRARY:
		vars[PLAYER_FMT_SOURCE].value.string = "library";
		break;
	case PLAYER_SOURCE_PLAYLIST:
		vars[PLAYER_FMT_SOURCE].value.string = "playlist";
		break;
	}
	XPTHREAD_MUTEX_UNLOCK(&player_source_mtx);

	/* Print the status. */
	option_lock();
	format = option_get_format("player-status-format");
	screen_player_status_printf(format, vars, PLAYER_FMT_NVARS);
	option_unlock();
}

/*
 * The player_state_mtx mutex must be locked before calling this function.
 */
static void
player_print_track(void)
{
	struct format *altfmt, *fmt;

	option_lock();
	track_lock_metadata();
	fmt = option_get_format("player-track-format");
	altfmt = option_get_format("player-track-format-alt");
	screen_player_track_printf(fmt, altfmt, player_track);
	track_unlock_metadata();
	option_unlock();
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
player_reopen_op(void)
{
	player_stop();

	XPTHREAD_MUTEX_LOCK(&player_op_mtx);
	if (player_op_opened) {
		LOG_INFO("reopening %s", player_op->name);
		player_op->close();
		if (player_op->open() != 0)
			player_op_opened = 0;
	}
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);
}

void
player_seek(int pos, int relative)
{
	unsigned int curpos;

	XPTHREAD_MUTEX_LOCK(&player_state_mtx);
	XPTHREAD_MUTEX_LOCK(&player_track_mtx);

	if (player_state == PLAYER_STATE_STOPPED)
		goto out;

	if (relative) {
		if (player_track->ip->get_position(player_track, &curpos))
			goto out;
		pos += curpos;
	}

	if (pos < 0)
		pos = 0;
	else if ((unsigned int)pos > player_track->duration)
		pos = player_track->duration;

	player_track->ip->seek(player_track, pos);

out:
	XPTHREAD_MUTEX_UNLOCK(&player_track_mtx);
	player_print_status();
	XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);
}

static void
player_set_signal_mask(void)
{
	sigset_t ss;

	sigfillset(&ss);
	pthread_sigmask(SIG_BLOCK, &ss, NULL);
}

void
player_set_source(enum player_source source)
{
	XPTHREAD_MUTEX_LOCK(&player_source_mtx);
	player_source = source;
	XPTHREAD_MUTEX_UNLOCK(&player_source_mtx);

	XPTHREAD_MUTEX_LOCK(&player_state_mtx);
	player_print_status();
	XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);
}

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
		if ((ret = player_op->get_volume()) == -1)
			goto out;

		volume += ret;
		if (volume < 0)
			volume = 0;
		else if (volume > 100)
			volume = 100;
	}

	player_op->set_volume(volume);

out:
	XPTHREAD_MUTEX_UNLOCK(&player_op_mtx);
	XPTHREAD_MUTEX_LOCK(&player_state_mtx);
	player_print_status();
	XPTHREAD_MUTEX_UNLOCK(&player_state_mtx);
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
