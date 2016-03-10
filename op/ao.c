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
static int		 op_ao_byte_format;
static int		 op_ao_driver_id;
static int		 op_ao_driver_type;

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
	option_add_string("ao-file", "", player_reopen_op);
	return 0;
}

static int
op_ao_open(void)
{
	ao_info		*info;
	char		*driver;

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

	op_ao_driver_type = info->type;

	if (info->preferred_byte_format != AO_FMT_NATIVE)
		op_ao_byte_format = info->preferred_byte_format;
	else {
		if (player_get_byte_order() == BYTE_ORDER_BIG)
			op_ao_byte_format = AO_FMT_BIG;
		else
			op_ao_byte_format = AO_FMT_LITTLE;
	}

	LOG_INFO("using %s driver", info->short_name);

	return 0;
}

static int
op_ao_start(struct sample_format *sf)
{
	ao_sample_format	 aosf;
	int			 error;
	char			*file;

	aosf.bits = sf->nbits;
	aosf.byte_format = op_ao_byte_format;
	aosf.channels = sf->nchannels;
	aosf.rate = sf->rate;
#ifdef HAVE_AO_MATRIX
	aosf.matrix = NULL;
#endif

	if (op_ao_driver_type == AO_TYPE_LIVE)
		op_ao_device = ao_open_live(op_ao_driver_id, &aosf, NULL);
	else {
		file = option_get_string("ao-file");
		op_ao_device = ao_open_file(op_ao_driver_id, file, 0, &aosf,
		    NULL);
		free(file);
	}

	if (op_ao_device == NULL) {
		error = errno;
		LOG_ERRX("ao_open_%s() failed: error %d",
		    op_ao_driver_type == AO_TYPE_LIVE ? "live" : "file",
		    error);

		switch (error) {
		/* Errors specific to live output drivers. */
		case AO_ENOTLIVE:
			msg_errx("Driver is not a live output driver");
			break;
		case AO_EOPENDEVICE:
			msg_errx("Cannot open device");
			break;

		/* Errors specific to file output drivers. */
		case AO_ENOTFILE:
			msg_errx("Driver is not a file output driver");
			break;
		case AO_EFILEEXISTS:
			msg_errx("Output file already exists");
			break;
		case AO_EOPENFILE:
			msg_errx("Cannot open output file");
			break;

		/* Common errors. */
		case AO_EBADFORMAT:
			msg_errx("Sample format not supported");
			break;
		case AO_EBADOPTION:
			msg_errx("An ao option has an invalid value");
			break;
		case AO_ENODRIVER:
			msg_errx("Cannot find driver");
			break;
		case AO_EFAIL:
		default:
			msg_errx("Unknown error");
			break;
		}
		return -1;
	}

	switch (aosf.byte_format) {
	case AO_FMT_BIG:
		sf->byte_order = BYTE_ORDER_BIG;
		break;
	case AO_FMT_LITTLE:
		sf->byte_order = BYTE_ORDER_LITTLE;
		break;
	case AO_FMT_NATIVE:
	default:
		sf->byte_order = player_get_byte_order();
		break;
	}

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
