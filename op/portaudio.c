/*
 * Copyright (c) 2015 Tim van der Molen <tim@kariliq.nl>
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

#include <portaudio.h>

#include "../siren.h"

#define OP_PORTAUDIO_BUFSIZE 4096

static void	 op_portaudio_close(void);
static size_t	 op_portaudio_get_buffer_size(void);
static int	 op_portaudio_get_volume_support(void);
static void	 op_portaudio_init(void);
static int	 op_portaudio_open(void);
static int	 op_portaudio_start(struct sample_format *);
static int	 op_portaudio_stop(void);
static int	 op_portaudio_write(void *, size_t);

const struct op	 op = {
	"portaudio",
	OP_PRIORITY_PORTAUDIO,
	op_portaudio_close,
	op_portaudio_get_buffer_size,
	NULL,
	op_portaudio_get_volume_support,
	op_portaudio_init,
	op_portaudio_open,
	NULL,
	op_portaudio_start,
	op_portaudio_stop,
	op_portaudio_write
};

PaStream	*op_portaudio_stream;
size_t		 op_portaudio_framesize;

static void
op_portaudio_close(void)
{
	PaError error;

	error = Pa_Terminate();
	if (error != paNoError) {
		LOG_ERRX("Pa_Terminate: %s", Pa_GetErrorText(error));
		msg_errx("Cannot terminate PortAudio: %s",
		    Pa_GetErrorText(error));
	}
}

static size_t
op_portaudio_get_buffer_size(void)
{
	return option_get_number("portaudio-buffer-size");
}

static int
op_portaudio_get_volume_support(void)
{
	return 0;
}

static void
op_portaudio_init(void)
{
	option_add_number("portaudio-buffer-size", OP_PORTAUDIO_BUFSIZE, 1,
	    INT_MAX, player_reopen_op);
}

static int
op_portaudio_open(void)
{
	PaError error;

	error = Pa_Initialize();
	if (error != paNoError) {
		LOG_ERRX("Pa_Initialize: %s", Pa_GetErrorText(error));
		msg_errx("Cannot initialise PortAudio: %s",
		    Pa_GetErrorText(error));
		return -1;
	}
	return 0;
}

static int
op_portaudio_start(struct sample_format *sf)
{
	const PaDeviceInfo	*devinfo;
	const PaHostApiInfo	*hostinfo;
	PaError			 error;

	devinfo = Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice());
	if (devinfo == NULL) {
		LOG_ERRX("Pa_GetDeviceInfo() failed");
		msg_errx("Cannot get device information");
		return -1;
	}

	hostinfo = Pa_GetHostApiInfo(devinfo->hostApi);
	if (hostinfo == NULL) {
		LOG_ERRX("Pa_GetHostApiInfo() failed");
		msg_errx("Cannot get host API information");
		return -1;
	}

	LOG_INFO("using %s device on %s host API", devinfo->name,
	    hostinfo->name);

	error = Pa_OpenDefaultStream(&op_portaudio_stream, 0, sf->nchannels,
	    paInt16, sf->rate, paFramesPerBufferUnspecified, NULL, NULL);
	if (error != paNoError) {
		LOG_ERRX("Pa_OpenDefaultStream: %s", Pa_GetErrorText(error));
		msg_errx("Cannot open stream: %s", Pa_GetErrorText(error));
		return -1;
	}

	error = Pa_StartStream(op_portaudio_stream);
	if (error != paNoError) {
		LOG_ERRX("Pa_StartStream: %s", Pa_GetErrorText(error));
		msg_errx("Cannot start stream: %s", Pa_GetErrorText(error));
		error = Pa_CloseStream(op_portaudio_stream);
		if (error != paNoError) {
			LOG_ERRX("Pa_CloseStream: %s", Pa_GetErrorText(error));
			msg_errx("Cannot close stream: %s",
			    Pa_GetErrorText(error));
		}
		return -1;
	}

	sf->byte_order = player_get_byte_order();
	op_portaudio_framesize = sf->nchannels * 2;
	return 0;
}

static int
op_portaudio_stop(void)
{
	PaError error;

	error = Pa_StopStream(op_portaudio_stream);
	if (error != paNoError) {
		LOG_ERRX("Pa_StopStream: %s", Pa_GetErrorText(error));
		msg_errx("Cannot stop stream: %s", Pa_GetErrorText(error));
		return -1;
	}

	error = Pa_CloseStream(op_portaudio_stream);
	if (error != paNoError) {
		LOG_ERRX("Pa_CloseStream: %s", Pa_GetErrorText(error));
		msg_errx("Cannot close stream: %s", Pa_GetErrorText(error));
		return -1;
	}

	return 0;
}

static int
op_portaudio_write(void *buf, size_t bufsize)
{
	PaError error;

	error = Pa_WriteStream(op_portaudio_stream, buf,
	    bufsize / op_portaudio_framesize);
	if (error != paNoError && error != paOutputUnderflowed) {
		LOG_ERRX("Pa_WriteStream: %s", Pa_GetErrorText(error));
		msg_errx("Playback error: %s", Pa_GetErrorText(error));
		return -1;
	}
	return 0;
}
