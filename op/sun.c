/*
 * Copyright (c) 2012 Tim van der Molen <tim@kariliq.nl>
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

#include <sys/ioctl.h>
#include <sys/types.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../siren.h"

/* NetBSD has <sys/audioio.h>; Solaris has <sys/audio.h>. */
#ifdef HAVE_SYS_AUDIO_H
#include <sys/audio.h>
#else
#include <sys/audioio.h>
#endif

#define OP_SUN_BUFSIZE	4096
#define OP_SUN_DEVICE	"/dev/audio"

#define OP_SUN_GAIN_TO_PERCENT(gain)					\
	((100 * ((gain) - AUDIO_MIN_GAIN) +				\
	(AUDIO_MAX_GAIN - AUDIO_MIN_GAIN + 1) / 2) /			\
	(AUDIO_MAX_GAIN - AUDIO_MIN_GAIN))
#define OP_SUN_PERCENT_TO_GAIN(percent)					\
	(((AUDIO_MAX_GAIN - AUDIO_MIN_GAIN) * (percent) + 50) / 100)

static void		 op_sun_close(void);
static size_t		 op_sun_get_buffer_size(void);
static int		 op_sun_get_volume(void);
static int		 op_sun_get_volume_support(void);
static void		 op_sun_init(void);
static int		 op_sun_open(void);
static void		 op_sun_set_volume(unsigned int);
static int		 op_sun_start(struct sample_format *);
static int		 op_sun_stop(void);
static int		 op_sun_write(struct sample_buffer *);

struct op		 op = {
	"sun",
	OP_PRIORITY_SUN,
	op_sun_close,
	op_sun_get_buffer_size,
	op_sun_get_volume,
	op_sun_get_volume_support,
	op_sun_init,
	op_sun_open,
	op_sun_set_volume,
	op_sun_start,
	op_sun_stop,
	op_sun_write
};

static int		 op_sun_fd;
static int		 op_sun_volume;
static char		*op_sun_device;

static void
op_sun_close(void)
{
	free(op_sun_device);
}

/* Return the buffer size in bytes. */
static size_t
op_sun_get_buffer_size(void)
{
	return OP_SUN_BUFSIZE;
}

static int
op_sun_get_volume(void)
{
	audio_info_t info;

	/* If the device hasn't been opened, then return the saved volume. */
	if (op_sun_fd == -1)
		return op_sun_volume;

	if (ioctl(op_sun_fd, AUDIO_GETINFO, &info) == -1) {
		LOG_ERR("ioctl: AUDIO_GETINFO");
		msg_err("Cannot get volume");
		return -1;
	}

	return OP_SUN_GAIN_TO_PERCENT(info.play.gain);
}

static int
op_sun_get_volume_support(void)
{
	return 1;
}

static void
op_sun_init(void)
{
	option_add_string("sun-device", OP_SUN_DEVICE, player_reopen_op);
}

static int
op_sun_open(void)
{
	op_sun_device = option_get_string("sun-device");
	LOG_INFO("using device %s", op_sun_device);

	op_sun_fd = open(op_sun_device, O_WRONLY);
	if (op_sun_fd == -1) {
		LOG_ERR("open: %s", op_sun_device);
		msg_err("Cannot open %s", op_sun_device);
		free(op_sun_device);
		return -1;
	}

	op_sun_volume = op_sun_get_volume();

	close(op_sun_fd);
	op_sun_fd = -1;

	if (op_sun_volume == -1) {
		free(op_sun_device);
		return -1;
	}

	return 0;
}

static void
op_sun_set_volume(unsigned int volume)
{
	audio_info_t info;

	if (op_sun_fd == -1) {
		msg_errx("Cannot change the volume level while the device is "
		    "closed");
		return;
	}

	AUDIO_INITINFO(&info);
	info.play.gain = OP_SUN_PERCENT_TO_GAIN(volume);

	if (ioctl(op_sun_fd, AUDIO_SETINFO, &info) == -1) {
		LOG_ERR("ioctl: AUDIO_SETINFO");
		msg_err("Cannot set volume");
	}
}

static int
op_sun_start(struct sample_format *sf)
{
	audio_info_t info;

	op_sun_fd = open(op_sun_device, O_WRONLY);
	if (op_sun_fd == -1) {
		LOG_ERR("open: %s", op_sun_device);
		msg_err("Cannot open %s", op_sun_device);
		return -1;
	}

	AUDIO_INITINFO(&info);
	info.play.channels = sf->nchannels;
	info.play.precision = sf->nbits;
	info.play.sample_rate = sf->rate;
#ifdef AUDIO_ENCODING_SLINEAR
	info.play.encoding = AUDIO_ENCODING_SLINEAR;
#else
	info.play.encoding = AUDIO_ENCODING_LINEAR;
#endif

	if (ioctl(op_sun_fd, AUDIO_SETINFO, &info) == -1) {
		LOG_ERR("ioctl: AUDIO_SETINFO");
		msg_err("Cannot set audio parameters");
		goto error;
	}

	if (ioctl(op_sun_fd, AUDIO_GETINFO, &info) == -1) {
		LOG_ERR("ioctl: AUDIO_GETINFO");
		msg_err("Cannot get audio parameters");
		goto error;
	}

	LOG_INFO("sample_rate=%u, channels=%u, precision=%u, encoding=%u",
	    info.play.sample_rate, info.play.channels, info.play.precision,
	    info.play.encoding);

	if (info.play.channels != sf->nchannels) {
		LOG_ERRX("%u channels not supported", sf->nchannels);
		msg_errx("%u channels not supported", sf->nchannels);
		goto error;
	}

	if (info.play.precision != sf->nbits) {
		LOG_ERRX("%u bits per sample not supported", precision);
		msg_errx("%u bits per sample not supported", precision);
		goto error;
	}

	/* Allow a 0.5% deviation in the sampling rate. */
	if (info.play.sample_rate < sf->rate * 995 / 1000 ||
	    info.play.sample_rate > sf->rate * 1005 / 1000) {
		LOG_ERRX("sampling rate (%u Hz) not supported", sf->rate);
		msg_errx("Sampling rate not supported");
		goto error;
	}

	switch (info.play.encoding) {
#ifdef AUDIO_ENCODING_SLINEAR_BE
	case AUDIO_ENCODING_SLINEAR_BE:
		sf->byte_order = BYTE_ORDER_BIG;
		break;
#endif
#ifdef AUDIO_ENCODING_SLINEAR_LE
	case AUDIO_ENCODING_SLINEAR_LE:
		sf->byte_order = BYTE_ORDER_LITTLE;
		break;
#endif
	case AUDIO_ENCODING_LINEAR:
		sf->byte_order = player_get_byte_order();
		break;
	default:
		LOG_ERRX("AUDIO_ENCODING_LINEAR not supported");
		msg_errx("Audio encoding not supported");
		goto error;
	}

	return 0;

error:
	close(op_sun_fd);
	op_sun_fd = -1;
	return -1;
}

static int
op_sun_stop(void)
{
	op_sun_volume = op_sun_get_volume();
	close(op_sun_fd);
	op_sun_fd = -1;
	return 0;
}

static int
op_sun_write(struct sample_buffer *sb)
{
	if (write(op_sun_fd, sb->data, sb->len_b) < 0) {
		LOG_ERR("write: %s", op_sun_device);
		msg_err("Playback error");
		return -1;
	}
	return 0;
}
