/*
 * Copyright (c) 2013 Tim van der Molen <tbvdm@xs4all.nl>
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

#include <string.h>

#include <alsa/asoundlib.h>

#include "../siren.h"

#define OP_ALSA_PCM_DEVICE	"default"
#define OP_ALSA_MIXER_DEVICE	"default"
#define OP_ALSA_MIXER_ELEM	"PCM"

static void		 op_alsa_close(void);
static size_t		 op_alsa_get_buffer_size(void);
static int		 op_alsa_get_volume(void);
static int		 op_alsa_get_volume_support(void);
static void		 op_alsa_init(void);
static int		 op_alsa_open(void);
static void		 op_alsa_set_volume(unsigned int);
static int		 op_alsa_start(struct sample_format *);
static int		 op_alsa_stop(void);
static int		 op_alsa_write(void *, size_t);

const struct op		 op = {
	"alsa",
	OP_PRIORITY_ALSA,
	op_alsa_close,
	op_alsa_get_buffer_size,
	op_alsa_get_volume,
	op_alsa_get_volume_support,
	op_alsa_init,
	op_alsa_open,
	op_alsa_set_volume,
	op_alsa_start,
	op_alsa_stop,
	op_alsa_write
};

static snd_pcm_t	*op_alsa_pcm_handle;
static snd_mixer_t	*op_alsa_mixer_handle;
static snd_mixer_elem_t	*op_alsa_mixer_elem;
static char		*op_alsa_mixer_dev;
static size_t		 op_alsa_bufsize;
static size_t		 op_alsa_framesize;

static void
op_alsa_close(void)
{
	(void)snd_pcm_close(op_alsa_pcm_handle);

	if (op_alsa_mixer_handle != NULL) {
		snd_mixer_free(op_alsa_mixer_handle);
		(void)snd_mixer_detach(op_alsa_mixer_handle,
		    op_alsa_mixer_dev);
		(void)snd_mixer_close(op_alsa_mixer_handle);
		free(op_alsa_mixer_dev);
	}
}

static size_t
op_alsa_get_buffer_size(void)
{
	return op_alsa_bufsize;
}

static int
op_alsa_get_volume(void)
{
	long int	volume;
	int		ret;

	if (op_alsa_mixer_handle == NULL)
		return -1;

	ret = snd_mixer_handle_events(op_alsa_mixer_handle);
	if (ret < 0)
		LOG_ERRX("snd_mixer_handle_events: %s", snd_strerror(ret));

	/*
	 * SND_MIXER_SCHN_MONO is an alias for SND_MIXER_SCHN_FRONT_LEFT. We
	 * assume all channels have the same value.
	 */
	ret = snd_mixer_selem_get_playback_volume(op_alsa_mixer_elem,
	    SND_MIXER_SCHN_MONO, &volume);
	if (ret) {
		LOG_ERRX("snd_mixer_get_playback_volume: %s",
		    snd_strerror(ret));
		msg_errx("Cannot get volume: %s", snd_strerror(ret));
		return -1;
	}

	return (int)volume;
}

static int
op_alsa_get_volume_support(void)
{
	return op_alsa_mixer_handle == NULL ? 0 : 1;
}

static void
op_alsa_init(void)
{
	option_add_string("alsa-mixer-device", OP_ALSA_MIXER_DEVICE, NULL);
	option_add_string("alsa-mixer-element", OP_ALSA_MIXER_ELEM, NULL);
	option_add_string("alsa-pcm-device", OP_ALSA_PCM_DEVICE, NULL);
}

static int
op_alsa_open(void)
{
	int	 ret;
	char	*dev, *elem;

	/*
	 * Open the PCM device.
	 */

	dev = option_get_string("alsa-pcm-device");

	ret = snd_pcm_open(&op_alsa_pcm_handle, dev, SND_PCM_STREAM_PLAYBACK,
	    0);
	if (ret) {
		LOG_ERRX("snd_pcm_open: %s: %s", dev, snd_strerror(ret));
		msg_errx("Cannot open device %s: %s", dev, snd_strerror(ret));
		free(dev);
		return -1;
	}

	LOG_INFO("using %s PCM device", dev);
	free(dev);

	/*
	 * Open the mixer device.
	 */

	op_alsa_mixer_handle = NULL;

	/* Open an empty mixer. */
	ret = snd_mixer_open(&op_alsa_mixer_handle, 0);
	if (ret) {
		LOG_ERRX("snd_mixer_open: %s", snd_strerror(ret));
		msg_errx("Cannot open mixer: %s", snd_strerror(ret));
		return 0;
	}

	op_alsa_mixer_dev = option_get_string("alsa-mixer-device");

	/* Attach to the mixer device. */
	ret = snd_mixer_attach(op_alsa_mixer_handle, op_alsa_mixer_dev);
	if (ret) {
		LOG_ERRX("snd_mixer_attach: %s: %s", op_alsa_mixer_dev,
		    snd_strerror(ret));
		msg_errx("Cannot attach to mixer device %s: %s",
		    op_alsa_mixer_dev, snd_strerror(ret));
		goto error1;
	}

	LOG_INFO("using %s mixer device", op_alsa_mixer_dev);

	/* Register mixer elements. */
	ret = snd_mixer_selem_register(op_alsa_mixer_handle, NULL, NULL);
	if (ret) {
		LOG_ERRX("snd_mixer_selem_register: %s", snd_strerror(ret));
		goto error2;
	}

	/* Load mixer elements. */
	ret = snd_mixer_load(op_alsa_mixer_handle);
	if (ret) {
		LOG_ERRX("snd_mixer_load: %s", snd_strerror(ret));
		goto error2;
	}

	elem = option_get_string("alsa-mixer-element");

	/* Search for the specified mixer element. */
	op_alsa_mixer_elem = snd_mixer_first_elem(op_alsa_mixer_handle);
	while (op_alsa_mixer_elem != NULL) {
		if (!strcmp(elem,
		    snd_mixer_selem_get_name(op_alsa_mixer_elem)))
			break;
		op_alsa_mixer_elem = snd_mixer_elem_next(op_alsa_mixer_elem);
	}

	if (op_alsa_mixer_elem == NULL) {
		LOG_ERRX("%s: mixer element not found", elem);
		msg_errx("Mixer element not found: %s", elem);
		free(elem);
		goto error3;
	}

	LOG_INFO("using %s mixer element", elem);
	free(elem);

	/* Check if the mixer element has a playback-volume control. */
	if (!snd_mixer_selem_has_playback_volume(op_alsa_mixer_elem)) {
		LOG_ERRX("mixer element does not have playback volume");
		goto error3;
	}

	/* Set the volume range to 0-100. */
	ret = snd_mixer_selem_set_playback_volume_range(op_alsa_mixer_elem, 0,
	    100);
	if (ret) {
		LOG_ERRX("snd_mixer_selem_set_playback_volume_range: %s",
		    snd_strerror(ret));
		goto error3;
	}

	return 0;

error3:
	snd_mixer_free(op_alsa_mixer_handle);

error2:
	(void)snd_mixer_detach(op_alsa_mixer_handle, op_alsa_mixer_dev);

error1:
	(void)snd_mixer_close(op_alsa_mixer_handle);
	op_alsa_mixer_handle = NULL;
	free(op_alsa_mixer_dev);

	return 0;
}

static void
op_alsa_set_volume(unsigned int volume)
{
	int ret;

	if (op_alsa_mixer_handle == NULL)
		return;

	ret = snd_mixer_selem_set_playback_volume_all(op_alsa_mixer_elem,
	    (long)volume);
	if (ret) {
		LOG_ERRX("snd_mixer_selem_set_playback_volume_all: %s",
		    snd_strerror(ret));
		msg_errx("Cannot set volume: %s", snd_strerror(ret));
	}
}

static int
op_alsa_start(struct sample_format *sf)
{
	snd_pcm_hw_params_t	*params;
	snd_pcm_format_t	 format;
	snd_pcm_uframes_t	 nframes;
	int			 dir, ret;
	unsigned int		 rate;

	/* Allocate memory. */
	ret = snd_pcm_hw_params_malloc(&params);
	if (ret) {
		LOG_ERRX("snd_pcm_hw_malloc: %s", snd_strerror(ret));
		goto error;
	}

	/* Set defaults. */
	(void)snd_pcm_hw_params_any(op_alsa_pcm_handle, params);

	/* Set access type. */
	ret = snd_pcm_hw_params_set_access(op_alsa_pcm_handle, params,
	    SND_PCM_ACCESS_RW_INTERLEAVED);
	if (ret) {
		LOG_ERRX("snd_pcm_hw_params_set_access: %s",
		    snd_strerror(ret));
		goto error;
	}

	/* Determine format. */
	sf->byte_order = player_get_byte_order();
	if (sf->byte_order == BYTE_ORDER_BIG)
		format = SND_PCM_FORMAT_S16_BE;
	else
		format = SND_PCM_FORMAT_S16_LE;

	/* Set format. */
	ret = snd_pcm_hw_params_set_format(op_alsa_pcm_handle, params, format);
	if (ret) {
		LOG_ERRX("snd_pcm_hw_params_set: %s", snd_strerror(ret));
		goto error;
	}

	/* Set number of channels. */
	ret = snd_pcm_hw_params_set_channels(op_alsa_pcm_handle, params,
	    sf->nchannels);
	if (ret) {
		LOG_ERRX("snd_pcm_hw_params_set_channels: %s",
		    snd_strerror(ret));
		goto error;
	}

	/* Set sampling rate. */
	dir = 0;
	rate = sf->rate;
	ret = snd_pcm_hw_params_set_rate_near(op_alsa_pcm_handle, params,
	    &rate, &dir);
	if (ret) {
		LOG_ERRX("snd_pcm_hw_params_set_rate_near: %s",
		    snd_strerror(ret));
		goto error;
	}

	/* Configure the device. */
	ret = snd_pcm_hw_params(op_alsa_pcm_handle, params);
	if (ret) {
		LOG_ERRX("snd_pcm_hw_params: %s", snd_strerror(ret));
		goto error;
	}

	/*
	 * The ALSA application buffer is divided into periods. Determine the
	 * size of 1 period and use that as the size of our buffer.
	 */
	(void)snd_pcm_hw_params_get_period_size(params, &nframes, &dir);
	op_alsa_framesize = (sf->nbits / 8) * sf->nchannels;
	op_alsa_bufsize = (size_t)nframes * op_alsa_framesize;

	snd_pcm_hw_params_free(params);

	LOG_DEBUG("format=%s, channels=%u, rate=%u, bufsize=%zu",
	    snd_pcm_format_name(format), sf->nchannels, rate, op_alsa_bufsize);
	return 0;

error:
	snd_pcm_hw_params_free(params);
	msg_errx("Cannot start playback: %s", snd_strerror(ret));
	return -1;
}

static int
op_alsa_stop(void)
{
	(void)snd_pcm_drain(op_alsa_pcm_handle);
	return 0;
}

static int
op_alsa_write(void *buf, size_t bufsize)
{
	snd_pcm_sframes_t ret;

	ret = snd_pcm_writei(op_alsa_pcm_handle, buf, bufsize /
	    op_alsa_framesize);
	if (ret == -EPIPE) {
		/* An underrun occurred; attempt to recover. */
		LOG_ERRX("snd_pcm_writei: %s", snd_strerror(ret));
		ret = snd_pcm_prepare(op_alsa_pcm_handle);
		if (ret) {
			LOG_ERRX("snd_pcm_prepare: %s", snd_strerror(ret));
			msg_errx("Playback error: %s", snd_strerror(ret));
			return -1;
		}
	} else if (ret < 0) {
		LOG_ERRX("snd_pcm_writei: %s", snd_strerror(ret));
		msg_errx("Playback error: %s", snd_strerror(ret));
		return -1;
	}
	return 0;
}
