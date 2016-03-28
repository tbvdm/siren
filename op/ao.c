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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include <ao/ao.h>

#include "../siren.h"

#define OP_AO_BUFSIZE	4096

static void		 op_ao_close(void);
static size_t		 op_ao_get_buffer_size(void);
static int		 op_ao_get_volume_support(void);
static int		 op_ao_init(void);
static int		 op_ao_open(void);
static int		 op_ao_start(struct sample_format *);
static int		 op_ao_stop(void);
static int		 op_ao_write(struct sample_buffer *);

const struct op		 op = {
	"ao",
	OP_PRIORITY_AO,
	op_ao_close,
	op_ao_get_buffer_size,
	NULL,
	op_ao_get_volume_support,
	op_ao_init,
	op_ao_open,
	NULL,
	op_ao_start,
	op_ao_stop,
	op_ao_write
};

static ao_device	*op_ao_device;
static int		 op_ao_driver_id;

static void
op_ao_close(void)
{
	ao_shutdown();
}

/* Return the buffer size in bytes. */
static size_t
op_ao_get_buffer_size(void)
{
	return option_get_number("ao-buffer-size");
}

static int
op_ao_get_volume_support(void)
{
	return 0;
}

static int
op_ao_init(void)
{
	option_add_number("ao-buffer-size", OP_AO_BUFSIZE, 1, INT_MAX,
	    player_reopen_op);
	option_add_string("ao-driver", "", player_reopen_op);
	return 0;
}

static int
op_ao_open(void)
{
	ao_info	*info;
	char	*driver;

	ao_initialize();

	driver = option_get_string("ao-driver");
	if (driver[0] == '\0') {
		if ((op_ao_driver_id = ao_default_driver_id()) == -1) {
			LOG_ERRX("ao_default_driver_id() failed");
			msg_errx("Cannot find default driver");
		}
	} else
		if ((op_ao_driver_id = ao_driver_id(driver)) == -1) {
			LOG_ERRX("ao_driver_id() failed");
			msg_errx("Cannot find %s driver", driver);
		}
	free(driver);

	if (op_ao_driver_id == -1) {
		ao_shutdown();
		return -1;
	}

	if ((info = ao_driver_info(op_ao_driver_id)) == NULL) {
		LOG_ERRX("ao_driver_info() failed");
		msg_errx("Cannot get driver information");
		ao_shutdown();
		return -1;
	}

	LOG_INFO("using %s driver", info->short_name);

	return 0;
}

static int
op_ao_start(struct sample_format *sf)
{
	ao_sample_format aosf;

	aosf.bits = sf->nbits;
	aosf.byte_format = AO_FMT_NATIVE;
	aosf.channels = sf->nchannels;
	aosf.rate = sf->rate;
#ifdef HAVE_AO_MATRIX
	aosf.matrix = NULL;
#endif

	if ((op_ao_device = ao_open_live(op_ao_driver_id, &aosf, NULL)) ==
	    NULL) {
		switch (errno) {
		case AO_ENOTLIVE:
			LOG_ERRX("ao_open_live: not a live output driver");
			msg_errx("Driver is not a live output driver");
			break;
		case AO_EOPENDEVICE:
			LOG_ERRX("ao_open_live: cannot open device");
			msg_errx("Cannot open device");
			break;
		case AO_EBADFORMAT:
			LOG_ERRX("ao_open_live: unsupported sample format");
			msg_errx("Sample format not supported");
			break;
		case AO_EBADOPTION:
			LOG_ERRX("ao_open_live: invalid option value");
			msg_errx("An ao option has an invalid value");
			break;
		case AO_ENODRIVER:
			LOG_ERRX("ao_open_live: cannot find driver");
			msg_errx("Cannot find driver");
			break;
		case AO_EFAIL:
		default:
			LOG_ERRX("ao_open_live: unknown error");
			msg_errx("Unknown error");
			break;
		}
		return -1;
	}

	sf->byte_order = player_get_byte_order();

	LOG_INFO("bits=%d, rate=%d, channels=%d, byte_format=%d", aosf.bits,
	    aosf.rate, aosf.channels, aosf.byte_format);

	return 0;
}

static int
op_ao_stop(void)
{
	if (ao_close(op_ao_device) == 0) {
		LOG_ERRX("ao_close() failed");
		msg_errx("Cannot close device");
		return -1;
	}
	return 0;
}

static int
op_ao_write(struct sample_buffer *sb)
{
	if (ao_play(op_ao_device, sb->data, sb->len_b) == 0) {
		LOG_ERRX("ao_play() failed");
		msg_errx("Playback error");
		return -1;
	}
	return 0;
}
