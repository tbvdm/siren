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

#include <limits.h>

#include <pulse/error.h>
#include <pulse/simple.h>

#include "../siren.h"

#define OP_PULSE_BUFSIZE 4096

static void		 op_pulse_close(void);
static size_t		 op_pulse_get_buffer_size(void);
static int		 op_pulse_get_volume_support(void);
static void		 op_pulse_init(void);
static int		 op_pulse_open(void);
static int		 op_pulse_start(struct sample_format *);
static int		 op_pulse_stop(void);
static int		 op_pulse_write(void *, size_t);

const struct op		 op = {
	"pulse",
	OP_PRIORITY_PULSE,
	op_pulse_close,
	op_pulse_get_buffer_size,
	NULL,
	op_pulse_get_volume_support,
	op_pulse_init,
	op_pulse_open,
	NULL,
	op_pulse_start,
	op_pulse_stop,
	op_pulse_write
};

static pa_simple	*op_pulse_conn;

static void
op_pulse_close(void)
{
}

/* Return the buffer size in bytes. */
static size_t
op_pulse_get_buffer_size(void)
{
	return option_get_number("pulse-buffer-size");
}

static int
op_pulse_get_volume_support(void)
{
	return 0;
}

static void
op_pulse_init(void)
{
	option_add_number("pulse-buffer-size", OP_PULSE_BUFSIZE, 1, INT_MAX,
	    player_reopen_op);
}

static int
op_pulse_open(void)
{
	return 0;
}

static int
op_pulse_start(struct sample_format *sf)
{
	pa_sample_spec	spec;
	int		error;

	sf->byte_order = player_get_byte_order();
	spec.format = sf->byte_order == BYTE_ORDER_BIG ? PA_SAMPLE_S16BE :
	    PA_SAMPLE_S16LE;
	spec.channels = sf->nchannels;
	spec.rate = sf->rate;

	if ((op_pulse_conn = pa_simple_new(NULL, "Siren", PA_STREAM_PLAYBACK,
	    NULL, "Siren", &spec, NULL, NULL, &error)) == NULL) {
		LOG_ERRX("pa_simple_new: %s", pa_strerror(error));
		msg_errx("Cannot connect to server: %s", pa_strerror(error));
		return -1;
	}

	LOG_INFO("format=%s, rate=%u, channels=%u",
	    pa_sample_format_to_string(spec.format), spec.rate,
	    spec.channels);

	return 0;
}

static int
op_pulse_stop(void)
{
	int error;

	error = 0;
	if (pa_simple_drain(op_pulse_conn, &error) < 0) {
		LOG_ERRX("pa_simple_drain: %s", pa_strerror(error));
		msg_errx("%s", pa_strerror(error));
	}

	pa_simple_free(op_pulse_conn);
	return error ? -1 : 0;
}

static int
op_pulse_write(void *buf, size_t bufsize)
{
	int error;

	if (pa_simple_write(op_pulse_conn, buf, bufsize, &error) < 0) {
		LOG_ERRX("pa_simple_write: %s", pa_strerror(error));
		msg_errx("Playback error: %s", pa_strerror(error));
		return -1;
	}
	return 0;
}
