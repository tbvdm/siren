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

#include "../config.h"

#include <sndio.h>
#include <stdlib.h>

#include "../siren.h"

#define OP_SNDIO_PERCENT_TO_VOLUME(p)	((SIO_MAXVOL * (p) + 50) / 100)
#define OP_SNDIO_VOLUME_TO_PERCENT(v)	\
	((100 * (v) + ((SIO_MAXVOL + 1) / 2)) / SIO_MAXVOL)

static void		 op_sndio_close(void);
static size_t		 op_sndio_get_buffer_size(void);
static int		 op_sndio_get_volume(void);
static int		 op_sndio_get_volume_support(void);
static int		 op_sndio_init(void);
static int		 op_sndio_open(void);
static void		 op_sndio_set_volume(unsigned int);
static int		 op_sndio_start(struct sample_format *);
static int		 op_sndio_stop(void);
static void		 op_sndio_volume_cb(void *, unsigned int);
static int		 op_sndio_write(struct sample_buffer *);

struct op		 op = {
	"sndio",
	OP_PRIORITY_SNDIO,
	"inet unix dns audio",
	op_sndio_close,
	op_sndio_get_buffer_size,
	op_sndio_get_volume,
	op_sndio_get_volume_support,
	op_sndio_init,
	op_sndio_open,
	op_sndio_set_volume,
	op_sndio_start,
	op_sndio_stop,
	op_sndio_write
};

static struct sio_hdl	*op_sndio_handle;
static struct sio_par	 op_sndio_par;
static unsigned int	 op_sndio_volume;
static int		 op_sndio_volume_support;

static void
op_sndio_close(void)
{
	sio_close(op_sndio_handle);
}

/* Return the buffer size in bytes. */
static size_t
op_sndio_get_buffer_size(void)
{
	/*
	 * The appbufsz parameter is specified in frames, so multiply it with
	 * the number of channels and the number of bytes per sample to get the
	 * buffer size in bytes.
	 */
	return op_sndio_par.appbufsz * op_sndio_par.pchan * op_sndio_par.bps;
}

static int
op_sndio_get_volume(void)
{
	return op_sndio_volume;
}

static int
op_sndio_get_volume_support(void)
{
	return op_sndio_volume_support;
}

static int
op_sndio_init(void)
{
	option_add_string("sndio-device", SIO_DEVANY, player_reopen_op);
	return 0;
}

static int
op_sndio_open(void)
{
	char *device;

	device = option_get_string("sndio-device");
	LOG_INFO("using %s device", device);
	op_sndio_handle = sio_open(device, SIO_PLAY, 0);
	free(device);

	if (op_sndio_handle == NULL) {
		LOG_ERRX("sio_open() failed");
		msg_errx("Cannot open stream");
		return -1;
	}

	if (!sio_onvol(op_sndio_handle, op_sndio_volume_cb, NULL))
		op_sndio_volume_support = 0;
	else
		op_sndio_volume_support = 1;

	return 0;
}

static void
op_sndio_set_volume(unsigned int volume)
{
	if (!sio_setvol(op_sndio_handle, OP_SNDIO_PERCENT_TO_VOLUME(volume))) {
		LOG_ERRX("sio_setvol() failed");
		msg_errx("Cannot set volume");
	}
}

static int
op_sndio_start(struct sample_format *sf)
{
	sio_initpar(&op_sndio_par);
	op_sndio_par.bits = sf->nbits;
	op_sndio_par.pchan = sf->nchannels;
	op_sndio_par.rate = sf->rate;
	op_sndio_par.sig = 1U;

	if (!sio_setpar(op_sndio_handle, &op_sndio_par)) {
		LOG_ERRX("sio_setpar() failed");
		msg_errx("Cannot set stream parameters");
		return -1;
	}

	if (!sio_getpar(op_sndio_handle, &op_sndio_par)) {
		LOG_ERRX("sio_getpar() failed");
		msg_errx("Cannot get stream parameters");
		return -1;
	}

	if (op_sndio_par.bits != sf->nbits ||
	    op_sndio_par.pchan != sf->nchannels ||
	    op_sndio_par.sig != 1U) {
		LOG_ERRX("cannot negotiate stream parameters");
		msg_errx("Cannot negotiate stream parameters");
		return -1;
	}

	/* Allow a 0.5% deviation in the sampling rate. */
	if (op_sndio_par.rate < sf->rate * 995 / 1000 ||
	    op_sndio_par.rate > sf->rate * 1005 / 1000) {
		LOG_ERRX("cannot set sampling rate");
		msg_errx("Cannot set sampling rate");
		return -1;
	}

	sf->byte_order = op_sndio_par.le ? BYTE_ORDER_LITTLE : BYTE_ORDER_BIG;

	LOG_INFO("bits=%u, bps=%u, sig=%u, le=%u, pchan=%u, rate=%u, "
	    "appbufsz=%u",
	    op_sndio_par.bits, op_sndio_par.bps, op_sndio_par.sig,
	    op_sndio_par.le, op_sndio_par.pchan, op_sndio_par.rate,
	    op_sndio_par.appbufsz);

	if (!sio_start(op_sndio_handle)) {
		LOG_ERRX("sio_start() failed");
		msg_errx("Cannot start stream");
		return -1;
	}

	return 0;
}

static int
op_sndio_stop(void)
{
	if (!sio_stop(op_sndio_handle)) {
		LOG_ERRX("sio_stop() failed");
		msg_errx("Cannot stop stream");
		return -1;
	}
	return 0;
}

static void
op_sndio_volume_cb(UNUSED void *p, unsigned int volume)
{
	if (volume != OP_SNDIO_PERCENT_TO_VOLUME(op_sndio_volume))
		op_sndio_volume = OP_SNDIO_VOLUME_TO_PERCENT(volume);
}

static int
op_sndio_write(struct sample_buffer *sb)
{
	size_t nwritten;

	nwritten = sio_write(op_sndio_handle, sb->data, sb->len_b);
	if (nwritten != sb->len_b)
		LOG_ERRX("only %zu of %zu bytes written", nwritten, sb->len_b);
	return 0;
}
