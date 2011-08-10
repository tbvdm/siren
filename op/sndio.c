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

#include <sndio.h>
#include <stdlib.h>

#include "../siren.h"

#define OP_SNDIO_ERROR_GETPAR		-1
#define OP_SNDIO_ERROR_NEGOTIATE	-2
#define OP_SNDIO_ERROR_OPEN		-3
#define OP_SNDIO_ERROR_SETPAR		-4
#define OP_SNDIO_ERROR_SETVOL		-5
#define OP_SNDIO_ERROR_START		-6
#define OP_SNDIO_ERROR_STOP		-7

#define OP_SNDIO_PERCENT_TO_VOLUME(p)	((SIO_MAXVOL * (p) + 50) / 100)
#define OP_SNDIO_VOLUME_TO_PERCENT(v)	\
	((100 * (v) + ((SIO_MAXVOL + 1) / 2)) / SIO_MAXVOL)

static void		 op_sndio_close(void);
static const char	*op_sndio_error(int);
static size_t		 op_sndio_get_buffer_size(void);
static int		 op_sndio_get_volume(void);
static int		 op_sndio_get_volume_support(void);
static void		 op_sndio_init(void);
static int		 op_sndio_open(void);
static int		 op_sndio_set_volume(unsigned int);
static int		 op_sndio_start(struct sample_format *);
static int		 op_sndio_stop(void);
static void		 op_sndio_volume_cb(void *, unsigned int);
static int		 op_sndio_write(void *, size_t);

struct op		 op = {
	"sndio",
	10,
	op_sndio_close,
	op_sndio_error,
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

static struct sio_hdl	*op_sndio_handle = NULL;
static struct sio_par	 op_sndio_par;
static unsigned int	 op_sndio_volume;
static int		 op_sndio_volume_support;

static void
op_sndio_close(void)
{
	sio_close(op_sndio_handle);
}

static const char *
op_sndio_error(int errnum)
{
	switch (errnum) {
	case OP_SNDIO_ERROR_GETPAR:
		return "Cannot get stream parameters";
	case OP_SNDIO_ERROR_NEGOTIATE:
		return "Cannot negotiate paramaters";
	case OP_SNDIO_ERROR_OPEN:
		return "Cannot open stream";
	case OP_SNDIO_ERROR_SETPAR:
		return "Cannot set stream parameters";
	case OP_SNDIO_ERROR_SETVOL:
		return "Cannot set volume";
	case OP_SNDIO_ERROR_START:
		return "Cannot start stream";
	case OP_SNDIO_ERROR_STOP:
		return "Cannot stop stream";
	default:
		LOG_DEBUG("%d: unknown error", errnum);
		return "Unknown error";
	}
}

static size_t
op_sndio_get_buffer_size(void)
{
	return (size_t)op_sndio_par.appbufsz;
}

static int
op_sndio_get_volume(void)
{
	return (int)op_sndio_volume;
}

static int
op_sndio_get_volume_support(void)
{
	return op_sndio_volume_support;
}

static void
op_sndio_init(void)
{
	option_add_string("sndio-device", "", NULL);
}

static int
op_sndio_open(void)
{
	struct sio_par	 par;
	char		*device;

	device = option_get_string("sndio-device");
	if (device[0] == '\0') {
		LOG_INFO("using default device");
		op_sndio_handle = sio_open(NULL, SIO_PLAY, 0);
	} else {
		LOG_INFO("using %s device", device);
		op_sndio_handle = sio_open(device, SIO_PLAY, 0);
	}
	free(device);

	if (op_sndio_handle == NULL) {
		LOG_ERRX("sio_open() failed");
		return OP_SNDIO_ERROR_OPEN;
	}

	sio_initpar(&par);
	if (!sio_getpar(op_sndio_handle, &par)) {
		LOG_ERRX("sio_getpar() failed");
		return OP_SNDIO_ERROR_GETPAR;
	}

	if (sio_onvol(op_sndio_handle, op_sndio_volume_cb, NULL) == 0)
		op_sndio_volume_support = 0;
	else
		op_sndio_volume_support = 1;

	return 0;
}

static int
op_sndio_set_volume(unsigned int volume)
{
	if (!sio_setvol(op_sndio_handle, OP_SNDIO_PERCENT_TO_VOLUME(volume))) {
		LOG_ERRX("sio_setvol() failed");
		return OP_SNDIO_ERROR_SETVOL;
	}

	return 0;
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
		return OP_SNDIO_ERROR_SETPAR;
	}

	if (!sio_getpar(op_sndio_handle, &op_sndio_par)) {
		LOG_ERRX("sio_getpar() failed");
		return OP_SNDIO_ERROR_GETPAR;
	}

	if (op_sndio_par.bits != sf->nbits ||
	    op_sndio_par.bps != 2U ||
	    op_sndio_par.pchan != sf->nchannels ||
	    op_sndio_par.rate != sf->rate ||
	    op_sndio_par.sig != 1U) {
		LOG_ERRX("cannot negotiate parameters");
		return OP_SNDIO_ERROR_NEGOTIATE;
	}

	sf->byte_order = op_sndio_par.le ? BYTE_ORDER_LITTLE : BYTE_ORDER_BIG;

	LOG_DEBUG("bits=%u, sig=%u, le=%u, pchan=%u, rate=%u, appbufsz=%u",
	    op_sndio_par.bits, op_sndio_par.sig, op_sndio_par.le,
	    op_sndio_par.pchan, op_sndio_par.rate, op_sndio_par.appbufsz);

	if (!sio_start(op_sndio_handle)) {
		LOG_ERRX("cannot start stream");
		return OP_SNDIO_ERROR_START;
	}

	return 0;
}

static int
op_sndio_stop(void)
{
	if (!sio_stop(op_sndio_handle)) {
		LOG_ERRX("sio_stop() failed");
		return OP_SNDIO_ERROR_STOP;
	}
	return 0;
}

/* ARGSUSED */
static void
op_sndio_volume_cb(UNUSED void *p, unsigned int volume)
{
	if (volume != OP_SNDIO_PERCENT_TO_VOLUME(op_sndio_volume))
		op_sndio_volume = OP_SNDIO_VOLUME_TO_PERCENT(volume);
}

static int
op_sndio_write(void *buf, size_t bufsize)
{
	size_t nwritten;

	if ((nwritten = sio_write(op_sndio_handle, buf, bufsize)) != bufsize)
		LOG_ERRX("only %zu of %zu bytes written", nwritten, bufsize);
	return 0;
}
