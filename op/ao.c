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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include <ao/ao.h>

#include "../siren.h"

#define OP_AO_BUFSIZE			4096

#define OP_AO_ERROR_CLOSE		-1
#define OP_AO_ERROR_DEVICE_OPEN		-2
#define OP_AO_ERROR_DRIVER		-3
#define OP_AO_ERROR_DRIVER_FILE		-4
#define OP_AO_ERROR_DRIVER_LIVE		-5
#define OP_AO_ERROR_FILE_EXISTS		-6
#define OP_AO_ERROR_FILE_OPEN		-7
#define OP_AO_ERROR_OPTION		-8
#define OP_AO_ERROR_OTHER		-9

static void		 op_ao_close(void);
static const char	*op_ao_error(int);
static size_t		 op_ao_get_buffer_size(void);
static int		 op_ao_get_volume_support(void);
static void		 op_ao_init(void);
static int		 op_ao_open(void);
static int		 op_ao_start(struct sample_format *);
static int		 op_ao_stop(void);
static int		 op_ao_write(void *, size_t);

const struct op		 op = {
	"ao",
	OP_PRIORITY_AO,
	op_ao_close,
	op_ao_error,
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

static const char *
op_ao_error(int errnum)
{
	switch (errnum) {
	case OP_AO_ERROR_CLOSE:
		return "Cannot close device or file";
	case OP_AO_ERROR_DEVICE_OPEN:
		return "Cannot open device";
	case OP_AO_ERROR_DRIVER:
		return "Cannot find driver";
	case OP_AO_ERROR_DRIVER_FILE:
		return "Driver is not a file output driver";
	case OP_AO_ERROR_DRIVER_LIVE:
		return "Driver is not a live output driver";
	case OP_AO_ERROR_FILE_EXISTS:
		return "Output file already exists";
	case OP_AO_ERROR_FILE_OPEN:
		return "Cannot open file";
	case OP_AO_ERROR_OPTION:
		return "Invalid option value";
	case OP_AO_ERROR_OTHER:
		return "Unspecified error";
	default:
		LOG_DEBUG("%d: unknown error", errnum);
		return "Unknown error";
	}
}

/* Return the buffer size in bytes. */
static size_t
op_ao_get_buffer_size(void)
{
	return (size_t)option_get_number("ao-buffer-size");
}

static int
op_ao_get_volume_support(void)
{
	return 0;
}

static void
op_ao_init(void)
{
	option_add_number("ao-buffer-size", OP_AO_BUFSIZE, 1, INT_MAX, NULL);
	option_add_string("ao-driver", "", NULL);
	option_add_string("ao-file", "", NULL);
}

static int
op_ao_open(void)
{
	ao_info		*info;
	char		*driver;

	ao_initialize();

	driver = option_get_string("ao-driver");
	if (driver[0] == '\0') {
		if ((op_ao_driver_id = ao_default_driver_id()) == -1)
			LOG_ERRX("ao_default_driver_id() failed");
	} else
		if ((op_ao_driver_id = ao_driver_id(driver)) == -1)
			LOG_ERRX("ao_driver_id() failed");
	free(driver);

	if (op_ao_driver_id == -1) {
		ao_shutdown();
		return OP_AO_ERROR_DRIVER;
	}

	if ((info = ao_driver_info(op_ao_driver_id)) == NULL) {
		LOG_ERRX("ao_driver_info() failed");
		ao_shutdown();
		return OP_AO_ERROR_DRIVER;
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
	const char		*file;

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
	}

	if (op_ao_device == NULL) {
		error = errno;
		LOG_ERRX("ao_open_%s() failed",
		    op_ao_driver_type == AO_TYPE_LIVE ? "live" : "file");

		switch (error) {
		/* Errors specific to live output drivers. */
		case AO_ENOTLIVE:
			return OP_AO_ERROR_DRIVER_LIVE;
		case AO_EOPENDEVICE:
			return OP_AO_ERROR_DEVICE_OPEN;

		/* Errors specific to file output drivers. */
		case AO_ENOTFILE:
			return OP_AO_ERROR_DRIVER_FILE;
		case AO_EFILEEXISTS:
			return OP_AO_ERROR_FILE_EXISTS;
		case AO_EOPENFILE:
			return OP_AO_ERROR_FILE_OPEN;

		/* Common errors. */
		case AO_EBADOPTION:
			return OP_AO_ERROR_OPTION;
		case AO_ENODRIVER:
			return OP_AO_ERROR_DRIVER;
		case AO_EFAIL:
		default:
			return OP_AO_ERROR_OTHER;
		}
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

	LOG_DEBUG("bits=%d, rate=%d, channels=%d, byte_format=%d", aosf.bits,
	    aosf.rate, aosf.channels, aosf.byte_format);

	return 0;
}

static int
op_ao_stop(void)
{
	if (ao_close(op_ao_device) == 0) {
		LOG_ERRX("ao_close() failed");
		return OP_AO_ERROR_CLOSE;
	}
	return 0;
}

static int
op_ao_write(void *buf, size_t bufsize)
{
	if (ao_play(op_ao_device, buf, bufsize) == 0)
		return -1;
	return 0;
}
