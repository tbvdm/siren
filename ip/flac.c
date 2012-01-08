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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <metadata.h>
#include <stream_decoder.h>

#include "../siren.h"

struct ip_flac_ipdata {
	FLAC__StreamDecoder *decoder;

	unsigned int	 cursample;

	const FLAC__int32 * const *buf;
	unsigned int	 bufidx;
	unsigned int	 buflen;
};

static void		 ip_flac_close(struct track *);
static int		 ip_flac_get_metadata(struct track *, char **);
static int		 ip_flac_get_position(struct track *, unsigned int *,
			    char **);
static int		 ip_flac_open(struct track *, char **);
static int		 ip_flac_read(struct track *, int16_t *, size_t,
			    char **);
static int		 ip_flac_seek(struct track *, unsigned int, char **);
static FLAC__StreamDecoderWriteStatus ip_flac_write_cb(
			    const FLAC__StreamDecoder *, const FLAC__Frame *,
			    const FLAC__int32 * const *, void *);

static const char	*ip_flac_error_status_to_string(
			    FLAC__StreamDecoderErrorStatus);
static const char	*ip_flac_init_status_to_string(
			    FLAC__StreamDecoderInitStatus);
static const char	*ip_flac_state_to_string(FLAC__StreamDecoderState);

static const char	*ip_flac_extensions[] = { "flac", NULL };

const struct ip		 ip = {
	"flac",
	ip_flac_extensions,
	ip_flac_close,
	ip_flac_get_metadata,
	ip_flac_get_position,
	ip_flac_open,
	ip_flac_read,
	ip_flac_seek
};

static void
ip_flac_close(struct track *t)
{
	struct ip_flac_ipdata *ipd;

	ipd = t->ipdata;
	(void)FLAC__stream_decoder_finish(ipd->decoder);
	FLAC__stream_decoder_delete(ipd->decoder);
	free(ipd);
}

/* ARGSUSED */
static void
ip_flac_error_cb(UNUSED const FLAC__StreamDecoder *decoder,
    FLAC__StreamDecoderErrorStatus error, void *tp)
{
	struct track *t;

	t = tp;
	LOG_ERRX("%s: %s", t->path, ip_flac_error_status_to_string(error));
}

static int
ip_flac_get_metadata(struct track *t, char **error)
{
	FLAC__StreamMetadata	 streaminfo, *comments;
	FLAC__uint32		 i;
	char			*comment;

	if (FLAC__metadata_get_tags(t->path, &comments) == false) {
		LOG_ERRX("%s: FLAC__metadata_get_tags() failed", t->path);
		*error = xstrdup("Cannot get metadata");
		return IP_ERROR_PLUGIN;
	}

	for (i = 0; i < comments->data.vorbis_comment.num_comments; i++) {
		comment = (char *)
		    comments->data.vorbis_comment.comments[i].entry;
		if (!strncasecmp(comment, "album=", 6)) {
			free(t->album);
			t->album = xstrdup(comment + 6);
		} else if (!strncasecmp(comment, "artist=", 7)) {
			free(t->artist);
			t->artist = xstrdup(comment + 7);
		} else if (!strncasecmp(comment, "date=", 5)) {
			free(t->date);
			t->date = xstrdup(comment + 5);
		} else if (!strncasecmp(comment, "genre=", 6)) {
			free(t->genre);
			t->genre = xstrdup(comment + 6);
		} else if (!strncasecmp(comment, "title=", 6)) {
			free(t->title);
			t->title = xstrdup(comment + 6);
		} else if (!strncasecmp(comment, "tracknumber=", 12)) {
			free(t->tracknumber);
			t->tracknumber = xstrdup(comment + 12);
		}
	}

	FLAC__metadata_object_delete(comments);

	if (FLAC__metadata_get_streaminfo(t->path, &streaminfo) == false) {
		LOG_ERRX("%s: FLAC__metadata_get_streaminfo() failed",
		    t->path);
		*error = xstrdup("Cannot get stream information");
		return IP_ERROR_PLUGIN;
	}

	if (streaminfo.data.stream_info.sample_rate != 0)
		t->duration =
		    (unsigned int)streaminfo.data.stream_info.total_samples /
		    streaminfo.data.stream_info.sample_rate;

	return 0;
}

/* ARGSUSED2 */
static int
ip_flac_get_position(struct track *t, unsigned int *pos, UNUSED char **error)
{
	struct ip_flac_ipdata *ipd;

	if (t->format.rate == 0)
		*pos = 0;
	else {
		ipd = t->ipdata;
		*pos = (ipd->cursample + ipd->bufidx) / t->format.rate;
	}

	return 0;
}

static int
ip_flac_open(struct track *t, char **error)
{
	struct ip_flac_ipdata		*ipd;
	FLAC__StreamDecoderInitStatus	 status;
	FLAC__StreamMetadata		 metadata;
	FILE				*fp;

	ipd = xmalloc(sizeof *ipd);

	if ((ipd->decoder = FLAC__stream_decoder_new()) == NULL) {
		LOG_ERRX("%s: FLAC__stream_decoder_new() failed", t->path);
		*error = xstrdup("Cannot allocate memory");
		free(ipd);
		return IP_ERROR_PLUGIN;
	}

	if ((fp = fopen(t->path, "r")) == NULL) {
		LOG_ERR("fopen: %s", t->path);
		free(ipd);
		return IP_ERROR_SYSTEM;
	}

	status = FLAC__stream_decoder_init_FILE(ipd->decoder, fp,
	    ip_flac_write_cb, NULL, ip_flac_error_cb, t);

	if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
		LOG_ERRX("FLAC__stream_decoder_init: %s: %s", t->path,
		    ip_flac_init_status_to_string(status));
		*error = xstrdup(ip_flac_init_status_to_string(status));
		FLAC__stream_decoder_delete(ipd->decoder);
		free(ipd);
		return IP_ERROR_PLUGIN;
	}

	if (FLAC__metadata_get_streaminfo(t->path, &metadata) == false) {
		LOG_ERRX("%s: FLAC__metadata_get_streaminfo() failed",
		    t->path);
		*error = xstrdup("Cannot get stream information");
		FLAC__stream_decoder_delete(ipd->decoder);
		free(ipd);
		return IP_ERROR_PLUGIN;
	}

	if (metadata.data.stream_info.bits_per_sample != 16) {
		LOG_ERRX("%s: %u: unsupported bit depth", t->path,
		    metadata.data.stream_info.bits_per_sample);
		*error = xstrdup("Unsupported bit depth");
		FLAC__stream_decoder_delete(ipd->decoder);
		free(ipd);
		return IP_ERROR_PLUGIN;
	}

	t->format.nbits = 16;
	t->format.nchannels = metadata.data.stream_info.channels;
	t->format.rate = metadata.data.stream_info.sample_rate;

	ipd->bufidx = 0;
	ipd->buflen = 0;
	ipd->cursample = 0;

	t->ipdata = ipd;
	return 0;
}

static int
ip_flac_read(struct track *t, int16_t *samples, size_t maxsamples,
    char **error)
{
	struct ip_flac_ipdata *ipd;
	FLAC__StreamDecoderState state;
	FLAC__bool	ret;
	size_t		nsamples;
	unsigned int	i;

	/* Sanity check. */
	if (maxsamples < (size_t)t->format.nchannels) {
		*error = xstrdup("Sample buffer too small");
		LOG_ERRX("%s: %s", t->path, *error);
		return IP_ERROR_PLUGIN;
	}

	ipd = t->ipdata;

	nsamples = 0;
	while (nsamples + (size_t)t->format.nchannels < maxsamples) {
		/* Fill the buffer. */
		while (ipd->bufidx == ipd->buflen) {
			/* Read a new frame. */
			ret = FLAC__stream_decoder_process_single(
			    ipd->decoder);
			state = FLAC__stream_decoder_get_state(ipd->decoder);

			if (state == FLAC__STREAM_DECODER_END_OF_STREAM)
				/* EOF. */
				return 0;

			if (ret == false) {
				/* Error. */
				*error = xstrdup(ip_flac_state_to_string(
				    state));
				LOG_ERRX("%s: %s", t->path, *error);
				return IP_ERROR_PLUGIN;
			}
		}

		for (i = 0; i < t->format.nchannels; i++)
			samples[nsamples++] =
			    (int16_t)ipd->buf[i][ipd->bufidx];
		ipd->bufidx++;
	}

	return (int)nsamples;
}

/* ARGSUSED */
static int
ip_flac_seek(struct track *t, unsigned int sec, char **error)
{
	struct ip_flac_ipdata *ipd;
	unsigned int nsamples, sample;

	ipd = t->ipdata;
	sample = sec * t->format.rate;
	nsamples = (unsigned int)FLAC__stream_decoder_get_total_samples(
	    ipd->decoder);

	if (sample >= nsamples)
		sample = nsamples > 0 ? nsamples - 1 : 0;

	if (FLAC__stream_decoder_seek_absolute(ipd->decoder,
	    (FLAC__uint64)sample) == false) {
		if (FLAC__stream_decoder_get_state(ipd->decoder) ==
		    FLAC__STREAM_DECODER_SEEK_ERROR) {
			(void)FLAC__stream_decoder_flush(ipd->decoder);
			ipd->bufidx = 0;
		}

		*error = xstrdup("Cannot seek");
		return IP_ERROR_PLUGIN;
	}

	ipd->cursample = sample;
	ipd->bufidx = ipd->buflen = 0;

	return 0;
}

/* ARGSUSED */
static FLAC__StreamDecoderWriteStatus
ip_flac_write_cb(UNUSED const FLAC__StreamDecoder *decoder,
    const FLAC__Frame *frame, const FLAC__int32 * const *buffer, void *tp)
{
	struct track *t;
	struct ip_flac_ipdata *ipd;

	t = tp;
	ipd = t->ipdata;

	if (frame->header.number_type == FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER)
		/* Fixed blocksize. */
		ipd->cursample += frame->header.blocksize;
	else
		/* Variable blocksize. */
		ipd->cursample = (unsigned int)
		    frame->header.number.sample_number;

	ipd->buf = buffer;
	ipd->bufidx = 0;
	ipd->buflen = frame->header.blocksize;

	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

/*
 * The FLAC__StreamDecoderErrorStatusString,
 * FLAC__StreamDecoderInitStatusString and FLAC__StreamDecoderStateString
 * string arrays do not provide very useful messages, so we use the messages
 * from the functions below instead. Their messages are based on information in
 * <http://flac.sourceforge.net/api/group__flac__stream__decoder.html>.
 */

static const char *
ip_flac_error_status_to_string(FLAC__StreamDecoderErrorStatus status)
{
	switch (status) {
	case FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC:
		return "Lost synchronisation";
	case FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER:
		return "Corrupted frame header";
	case FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH:
		return "Frame CRC mismatched";
	case FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM:
		return "Reserved fields encountered";
	default:
		return "Unknown error status";
	}
}

static const char *
ip_flac_init_status_to_string(FLAC__StreamDecoderInitStatus status)
{
	switch (status) {
	case FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER:
		return "Unsupported container format";
	case FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS:
		return "Required callback not supplied";
	case FLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR:
		return "Memory allocation error";
	case FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED:
		return "Already initialised";
	default:
		return "Unknown initialisation status";
	}
}

static const char *
ip_flac_state_to_string(FLAC__StreamDecoderState state)
{
	switch (state) {
	case FLAC__STREAM_DECODER_SEARCH_FOR_METADATA:
		return "Ready to search for metadata";
	case FLAC__STREAM_DECODER_READ_METADATA:
		return "Reading metadata or ready to do so";
	case FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC:
		return "Searching for frame sync code or ready to do so";
	case FLAC__STREAM_DECODER_READ_FRAME:
		return "Reading frame or ready to do so";
	case FLAC__STREAM_DECODER_END_OF_STREAM:
		return "End of stream reached";
	case FLAC__STREAM_DECODER_OGG_ERROR:
		return "Error in Ogg layer";
	case FLAC__STREAM_DECODER_SEEK_ERROR:
		return "Seek error";
	case FLAC__STREAM_DECODER_ABORTED:
		return "Aborted by read callback-function";
	case FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR:
		return "Memory allocation error";
	case FLAC__STREAM_DECODER_UNINITIALIZED:
		return "Not initialised";
	default:
		return "Unknown decoder state";
	}
}
