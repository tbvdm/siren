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
#include <unistd.h>

#include "../siren.h"

#ifdef HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#else
#include <soundcard.h>
#endif

#if defined(SNDCTL_DSP_GETPLAYVOL) && defined(SNDCTL_DSP_SETPLAYVOL)
#define OP_OSS_HAVE_VOLUME_SUPPORT
#endif

#define OP_OSS_BUFSIZE	4096
#define OP_OSS_DEVICE	"/dev/dsp"

static void		 op_oss_close(void);
static size_t		 op_oss_get_buffer_size(void);
static int		 op_oss_get_volume_support(void);
static int		 op_oss_init(void);
static int		 op_oss_open(void);
static int		 op_oss_start(struct sample_format *);
static int		 op_oss_stop(void);
static int		 op_oss_write(struct sample_buffer *);
#ifdef OP_OSS_HAVE_VOLUME_SUPPORT
static int		 op_oss_get_volume(void);
static void		 op_oss_set_volume(unsigned int);
#endif

struct op		 op = {
	"oss",
	OP_PRIORITY_OSS,
	op_oss_close,
	op_oss_get_buffer_size,
#ifdef OP_OSS_HAVE_VOLUME_SUPPORT
	op_oss_get_volume,
#else
	NULL,
#endif
	op_oss_get_volume_support,
	op_oss_init,
	op_oss_open,
#ifdef OP_OSS_HAVE_VOLUME_SUPPORT
	op_oss_set_volume,
#else
	NULL,
#endif
	op_oss_start,
	op_oss_stop,
	op_oss_write
};

static size_t		 op_oss_buffer_size;
static int		 op_oss_fd;
static char		*op_oss_device;
#ifdef OP_OSS_HAVE_VOLUME_SUPPORT
static int		 op_oss_volume;
#endif

static void
op_oss_close(void)
{
	free(op_oss_device);
}

/* Return the buffer size in bytes. */
static size_t
op_oss_get_buffer_size(void)
{
	return op_oss_buffer_size;
}

#ifdef OP_OSS_HAVE_VOLUME_SUPPORT
static int
op_oss_get_volume(void)
{
	int arg;

	/* If the device hasn't been opened, then return the saved volume. */
	if (op_oss_fd == -1)
		return op_oss_volume;

	if (ioctl(op_oss_fd, SNDCTL_DSP_GETPLAYVOL, &arg) == -1) {
		LOG_ERR("ioctl: SNDCTL_DSP_GETPLAYVOL");
		msg_err("Cannot get volume");
		return -1;
	}

	/*
	 * The two least significant bytes contain the volume levels for the
	 * left and the right channel, respectively. The two levels should have
	 * the same value, so we can use either one. The range is from 0 to 100
	 * inclusive.
	 */
	return arg & 0xff;
}
#endif

static int
op_oss_get_volume_support(void)
{
#ifdef OP_OSS_HAVE_VOLUME_SUPPORT
	return op_oss_volume == -1 ? 0 : 1;
#else
	return 0;
#endif
}

static int
op_oss_init(void)
{
	option_add_string("oss-device", OP_OSS_DEVICE, player_reopen_op);
	return 0;
}

static int
op_oss_open(void)
{
	op_oss_device = option_get_string("oss-device");
	LOG_INFO("using device %s", op_oss_device);

#ifdef OP_OSS_HAVE_VOLUME_SUPPORT
	op_oss_fd = open(op_oss_device, O_WRONLY);
	if (op_oss_fd == -1) {
		LOG_ERR("open: %s", op_oss_device);
		msg_err("Cannot open %s", op_oss_device);
		free(op_oss_device);
		return -1;
	}

	op_oss_volume = op_oss_get_volume();
	close(op_oss_fd);
	op_oss_fd = -1;
#endif

	return 0;
}

#ifdef OP_OSS_HAVE_VOLUME_SUPPORT
static void
op_oss_set_volume(unsigned int volume)
{
	int arg;

	if (op_oss_fd == -1)
		msg_errx("Cannot change the volume level while the device is "
		    "closed");
	else {
		/* Set the volume level for the left and right channels. */
		arg = volume | (volume << 8);
		if (ioctl(op_oss_fd, SNDCTL_DSP_SETPLAYVOL, &arg) == -1) {
			LOG_ERR("ioctl: SNDCTL_DSP_SETPLAYVOL");
			msg_err("Cannot set volume");
		}
	}
}
#endif

static int
op_oss_start(struct sample_format *sf)
{
	int arg, want_arg;

	op_oss_fd = open(op_oss_device, O_WRONLY);
	if (op_oss_fd == -1) {
		LOG_ERR("open: %s", op_oss_device);
		msg_err("Cannot open %s", op_oss_device);
		return -1;
	}

	/*
	 * The OSS 4 documentation recommends to set the number of channels
	 * first, then the sample format and then the sampling rate.
	 */

	/* Set number of channels. */
	arg = sf->nchannels;
	if (ioctl(op_oss_fd, SNDCTL_DSP_CHANNELS, &arg) == -1) {
		LOG_ERR("ioctl: SNDCTL_DSP_CHANNELS");
		msg_err("Cannot set number of channels");
		goto error;
	}
	if (arg != (int)sf->nchannels) {
		LOG_ERRX("%u channels not supported", sf->nchannels);
		msg_errx("%u channels not supported", sf->nchannels);
		goto error;
	}

	/* Set format. */
	if (sf->nbits <= 8)
		arg = AFMT_S8;
	else if (sf->nbits <= 16)
		arg = AFMT_S16_NE;
	else {
#ifdef AFMT_S32_NE
		arg = AFMT_S32_NE;
#else
		LOG_ERRX("%u bits per sample not supported", sf->nbits);
		msg_errx("%u bits per sample not supported", sf->nbits);
		goto error;
#endif
	}

	want_arg = arg;
	if (ioctl(op_oss_fd, SNDCTL_DSP_SETFMT, &arg) == -1) {
		LOG_ERR("ioctl: SNDCTL_DSP_SETFMT");
		msg_err("Cannot set audio format");
		goto error;
	}
	if (arg != want_arg) {
		LOG_ERRX("%d: audio format not supported", want_arg);
		msg_errx("Audio format not supported");
		goto error;
	}

	/* Set sampling rate. */
	arg = sf->rate;
	if (ioctl(op_oss_fd, SNDCTL_DSP_SPEED, &arg) == -1) {
		LOG_ERR("ioctl: SNDCTL_DSP_SPEED");
		msg_err("Cannot set sampling rate");
		goto error;
	}
	/* Allow a 0.5% deviation in the sampling rate. */
	if ((unsigned int)arg < sf->rate * 995 / 1000 ||
	    (unsigned int)arg > sf->rate * 1005 / 1000) {
		LOG_ERRX("sampling rate (%u Hz) not supported", sf->rate);
		msg_errx("Sampling rate not supported");
		goto error;
	}

	/* Set byte order of sample format. */
#if AFMT_S16_NE == AFMT_S16_BE
	sf->byte_order = BYTE_ORDER_BIG;
#else
	sf->byte_order = BYTE_ORDER_LITTLE;
#endif

	/*
	 * Determine the optimal buffer size. This is not relevant on OSS 4,
	 * but it is on older OSS versions.
	 */
	if (ioctl(op_oss_fd, SNDCTL_DSP_GETBLKSIZE, &arg) == -1) {
		LOG_ERR("ioctl: SNDCTL_DSP_GETBLKSIZE");
		op_oss_buffer_size = OP_OSS_BUFSIZE;
	} else
		op_oss_buffer_size = arg;

	return 0;

error:
	close(op_oss_fd);
	op_oss_fd = -1;
	return -1;
}

static int
op_oss_stop(void)
{
#ifdef OP_OSS_HAVE_VOLUME_SUPPORT
	int vol;

	/* Save the current volume level before closing the device. */
	if (op_oss_volume != -1) {
		vol = op_oss_get_volume();
		if (vol != -1)
			op_oss_volume = vol;
	}
#endif

	close(op_oss_fd);
	op_oss_fd = -1;
	return 0;
}

static int
op_oss_write(struct sample_buffer *sb)
{
	if (write(op_oss_fd, sb->data, sb->len_b) == -1) {
		LOG_ERR("write: %s", op_oss_device);
		msg_err("Playback error");
		return -1;
	}
	return 0;
}
