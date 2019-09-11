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

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_NEW_FLAC_INCLUDE_PATH
#include <FLAC/metadata.h>
#include <FLAC/stream_decoder.h>
#else
#include <metadata.h>
#include <stream_decoder.h>
#endif

#include "../siren.h"

#define IP_FLAC_ERROR	-1
#define IP_FLAC_EOF	0
#define IP_FLAC_OK	1

struct ip_flac_ipdata {
	FLAC__StreamDecoder *decoder;

	unsigned int	 cursample;

	const FLAC__int32 * const *buf;
	unsigned int	 bufidx;
	unsigned int	 buflen;
};

static void		 ip_flac_close(struct track *);
static void		 ip_flac_get_metadata(struct track *);
static int		 ip_flac_get_position(struct track *, unsigned int *);
static int		 ip_flac_open(struct track *);
static int		 ip_flac_read(struct track *, struct sample_buffer *);
static void		 ip_flac_seek(struct track *, unsigned int);
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
	IP_PRIORITY_FLAC,
	ip_flac_extensions,
	ip_flac_close,
	ip_flac_get_metadata,
	ip_flac_get_position,
	NULL,
	ip_flac_open,
	ip_flac_read,
	ip_flac_seek
};

static void
ip_flac_close(struct track *t)
{
	struct ip_flac_ipdata *ipd;

	ipd = t->ipdata;
	FLAC__stream_decoder_finish(ipd->decoder);
	FLAC__stream_decoder_delete(ipd->decoder);
	free(ipd);
}

static void
ip_flac_error_cb(UNUSED const FLAC__StreamDecoder *decoder,
    FLAC__StreamDecoderErrorStatus error, void *tp)
{
	struct track *t;

	t = tp;
	LOG_ERRX("%s: %s", t->path, ip_flac_error_status_to_string(error));
}

static int
ip_flac_fill_buffer(const char *path, struct ip_flac_ipdata *ipd)
{
	FLAC__bool			ret;
	FLAC__StreamDecoderState	state;

	ipd->bufidx = 0;
	ipd->buflen = 0;

	for (;;) {
		ret = FLAC__stream_decoder_process_single(ipd->decoder);
		state = FLAC__stream_decoder_get_state(ipd->decoder);

		if (ipd->buflen)
			return IP_FLAC_OK;
		if (state == FLAC__STREAM_DECODER_END_OF_STREAM)
			return IP_FLAC_EOF;
		if (ret == false) {
			LOG_ERRX("FLAC__stream_decoder_process_single: %s: %s",
			    path, ip_flac_state_to_string(state));
			msg_errx("Cannot read from track: %s",
			    ip_flac_state_to_string(state));
			return IP_FLAC_ERROR;
		}
	}
}

static void
ip_flac_get_metadata(struct track *t)
{
	FLAC__StreamMetadata	 streaminfo, *comments;
	FLAC__uint32		 i;

	if (FLAC__metadata_get_tags(t->path, &comments) == false) {
		LOG_ERRX("%s: FLAC__metadata_get_tags() failed", t->path);
		msg_errx("%s: Cannot get metadata", t->path);
		return;
	}

	for (i = 0; i < comments->data.vorbis_comment.num_comments; i++)
		track_copy_vorbis_comment(t,
		    (char *)comments->data.vorbis_comment.comments[i].entry);

	FLAC__metadata_object_delete(comments);

	if (FLAC__metadata_get_streaminfo(t->path, &streaminfo) == false) {
		LOG_ERRX("%s: FLAC__metadata_get_streaminfo() failed",
		    t->path);
		msg_errx("%s: Cannot get stream information", t->path);
		return;
	}

	if (streaminfo.data.stream_info.sample_rate != 0)
		t->duration = streaminfo.data.stream_info.total_samples /
		    streaminfo.data.stream_info.sample_rate;
}

static int
ip_flac_get_position(struct track *t, unsigned int *pos)
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
ip_flac_open(struct track *t)
{
	struct ip_flac_ipdata		*ipd;
	FLAC__StreamDecoderInitStatus	 status;
	FLAC__StreamMetadata		 metadata;
	FILE				*fp;

	ipd = xmalloc(sizeof *ipd);

	if ((ipd->decoder = FLAC__stream_decoder_new()) == NULL) {
		LOG_ERRX("%s: FLAC__stream_decoder_new() failed", t->path);
		msg_errx("%s: Cannot allocate memory for FLAC decoder",
		    t->path);
		goto error1;
	}

	if ((fp = fopen(t->path, "r")) == NULL) {
		LOG_ERR("fopen: %s", t->path);
		msg_err("%s: Cannot open track", t->path);
		goto error2;
	}

	status = FLAC__stream_decoder_init_FILE(ipd->decoder, fp,
	    ip_flac_write_cb, NULL, ip_flac_error_cb, t);

	if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
		LOG_ERRX("FLAC__stream_decoder_init: %s: %s", t->path,
		    ip_flac_init_status_to_string(status));
		msg_errx("%s: Cannot initialise FLAC decoder: %s", t->path,
		    ip_flac_init_status_to_string(status));
		fclose(fp);
		goto error2;
	}

	if (FLAC__metadata_get_streaminfo(t->path, &metadata) == false) {
		LOG_ERRX("%s: FLAC__metadata_get_streaminfo() failed",
		    t->path);
		msg_errx("%s: Cannot get stream information", t->path);
		goto error3;
	}

	t->format.nbits = metadata.data.stream_info.bits_per_sample;
	t->format.nchannels = metadata.data.stream_info.channels;
	t->format.rate = metadata.data.stream_info.sample_rate;

	ipd->bufidx = 0;
	ipd->buflen = 0;
	ipd->cursample = 0;

	t->ipdata = ipd;
	return 0;

error3:
	FLAC__stream_decoder_finish(ipd->decoder);
error2:
	FLAC__stream_decoder_delete(ipd->decoder);
error1:
	free(ipd);
	return -1;
}

static int
ip_flac_read(struct track *t, struct sample_buffer *sb)
{
	struct ip_flac_ipdata	*ipd;
	size_t			 i;
	unsigned int		 j;
	int			 ret;

	ipd = t->ipdata;

	i = 0;
	while (i + t->format.nchannels <= sb->size_s) {
		if (ipd->bufidx == ipd->buflen) {
			ret = ip_flac_fill_buffer(t->path, ipd);
			if (ret == IP_FLAC_EOF)
				break;
			if (ret == IP_FLAC_ERROR)
				return -1;
		}

		switch (sb->nbytes) {
		case 1:
			for (j = 0; j < t->format.nchannels; j++)
				sb->data1[i++] = ipd->buf[j][ipd->bufidx];
			break;
		case 2:
			for (j = 0; j < t->format.nchannels; j++)
				sb->data2[i++] = ipd->buf[j][ipd->bufidx];
			break;
		case 4:
			for (j = 0; j < t->format.nchannels; j++)
				sb->data4[i++] = ipd->buf[j][ipd->bufidx];
			break;
		}

		ipd->bufidx++;
	}

	sb->len_s = i;
	sb->len_b = sb->len_s * sb->nbytes;
	return sb->len_s != 0;
}

static void
ip_flac_seek(struct track *t, unsigned int sec)
{
	struct ip_flac_ipdata *ipd;
	FLAC__StreamDecoderState state;
	unsigned int nsamples, sample;

	ipd = t->ipdata;
	sample = sec * t->format.rate;
	nsamples = FLAC__stream_decoder_get_total_samples(ipd->decoder);

	if (sample >= nsamples)
		sample = nsamples > 0 ? nsamples - 1 : 0;

	if (FLAC__stream_decoder_seek_absolute(ipd->decoder, sample) ==
	    false) {
		state = FLAC__stream_decoder_get_state(ipd->decoder);

		LOG_ERRX("FLAC__stream_decoder_seek_absolute: %s: %s", t->path,
		    ip_flac_state_to_string(state));
		msg_errx("Cannot seek: %s", ip_flac_state_to_string(state));

		/* The decoder must be flushed after a seek error. */
		if (state == FLAC__STREAM_DECODER_SEEK_ERROR) {
			FLAC__stream_decoder_flush(ipd->decoder);
			ipd->bufidx = 0;
		}
	} else {
		ipd->cursample = sample;
		ipd->bufidx = 0;
		ipd->buflen = 0;
	}
}

static FLAC__StreamDecoderWriteStatus
ip_flac_write_cb(UNUSED const FLAC__StreamDecoder *decoder,
    const FLAC__Frame *frame, const FLAC__int32 * const *buffer, void *tp)
{
	struct track		*t;
	struct ip_flac_ipdata	*ipd;

	t = tp;
	ipd = t->ipdata;

	if (frame->header.number_type == FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER)
		/* Fixed blocksize. */
		ipd->cursample += frame->header.blocksize;
	else
		/* Variable blocksize. */
		ipd->cursample = frame->header.number.sample_number;

	ipd->buf = buffer;
	ipd->buflen = frame->header.blocksize;

	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

/*
 * The FLAC__StreamDecoderErrorStatusString,
 * FLAC__StreamDecoderInitStatusString and FLAC__StreamDecoderStateString
 * string arrays do not provide very useful messages, so we use the messages
 * from the functions below instead. The messages are based on information in
 * <https://www.xiph.org/flac/api/group__flac__stream__decoder.html>.
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
