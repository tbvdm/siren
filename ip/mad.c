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

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <id3tag.h>
#include <mad.h>

#include "../siren.h"

/*
 * This value must be equal to or larger than the largest possible MPEG audio
 * frame size, which, according to <http://marc.info/?m=113584709618786>, is
 * 2880 bytes.
 */
#define IP_MAD_BUFSIZE		65536

#define IP_MAD_ERROR		-1
#define IP_MAD_EOF		0
#define IP_MAD_OK		1

#define IP_MAD_NEED_REFILL(error) \
    ((error) == MAD_ERROR_BUFLEN || (error) == MAD_ERROR_BUFPTR)

struct ip_mad_ipdata {
	FILE			*fp;

	struct mad_stream	 stream;
	struct mad_frame	 frame;
	struct mad_synth	 synth;
	mad_timer_t		 timer;

	unsigned short int	 sampleidx;
	unsigned char		*buf;
};

static void		 ip_mad_close(struct track *);
static int		 ip_mad_decode_frame_header(FILE *,
			    struct mad_stream *, struct mad_header *,
			    unsigned char *, size_t);
static int		 ip_mad_fill_stream(FILE *, struct mad_stream *,
			    unsigned char *, size_t);
static int		 ip_mad_get_position(struct track *, unsigned int *);
static int		 ip_mad_get_metadata(struct track *);
static int		 ip_mad_open(struct track *);
static int		 ip_mad_read(struct track *, int16_t *, size_t);
static void		 ip_mad_seek(struct track *, unsigned int);

static const char	*ip_mad_extensions[] = { "mp1", "mp2", "mp3", NULL };

const struct ip		 ip = {
	"mad",
	ip_mad_extensions,
	ip_mad_close,
	ip_mad_get_metadata,
	ip_mad_get_position,
	ip_mad_open,
	ip_mad_read,
	ip_mad_seek
};

/*
 * Calculate the duration in seconds of a file by reading it from start to end
 * and summing the duration of all frames in it. This method is rather
 * expensive and therefore used only if other methods have failed.
 */
static unsigned int
ip_mad_calculate_duration(const char *file)
{
	FILE			*fp;
	struct mad_stream	 stream;
	struct mad_header	 header;
	mad_timer_t		 timer;
	int			 ret;
	unsigned char		*buf;

	if ((fp = fopen(file, "r")) == NULL) {
		LOG_ERR("fopen: %s", file);
		msg_err("%s: Cannot open track", file);
		return 0;
	}

	mad_stream_init(&stream);
	mad_header_init(&header);
	mad_timer_reset(&timer);
	buf = xmalloc(IP_MAD_BUFSIZE + MAD_BUFFER_GUARD);

	/* Read the whole file and sum the duration of all frames. */
	while ((ret = ip_mad_decode_frame_header(fp, &stream, &header, buf,
	    IP_MAD_BUFSIZE)) == IP_MAD_OK)
		mad_timer_add(&timer, header.duration);

	free(buf);
	mad_header_finish(&header);
	mad_stream_finish(&stream);
	(void)fclose(fp);

	if (ret == IP_MAD_ERROR)
		return 0;

	return (unsigned int)mad_timer_count(timer, MAD_UNITS_SECONDS);
}

static void
ip_mad_close(struct track *t)
{
	struct ip_mad_ipdata *ipd;

	ipd = t->ipdata;

	mad_synth_finish(&ipd->synth);
	mad_frame_finish(&ipd->frame);
	mad_stream_finish(&ipd->stream);
	(void)fclose(ipd->fp);

	free(ipd->buf);
	free(ipd);
}

static int
ip_mad_decode_frame(struct ip_mad_ipdata *ipd)
{
	int ret;

	for (;;) {
		if (mad_frame_decode(&ipd->frame, &ipd->stream) == 0) {
			mad_synth_frame(&ipd->synth, &ipd->frame);
			ipd->sampleidx = 0;
			return IP_MAD_OK;
		}
		if (IP_MAD_NEED_REFILL(ipd->stream.error)) {
			ret = ip_mad_fill_stream(ipd->fp, &ipd->stream,
			    ipd->buf, IP_MAD_BUFSIZE);
			if (ret == IP_MAD_EOF || ret == IP_MAD_ERROR)
				return ret;
		} else if (!MAD_RECOVERABLE(ipd->stream.error)) {
			LOG_ERRX("mad_frame_decode: %s",
			    mad_stream_errorstr(&ipd->stream));
			msg_errx("Cannot decode frame: %s",
			    mad_stream_errorstr(&ipd->stream));
			return IP_MAD_ERROR;
		}
	}
}

static int
ip_mad_decode_frame_header(FILE *fp, struct mad_stream *stream,
    struct mad_header *header, unsigned char *buf, size_t bufsize)
{
	int ret;

	for (;;) {
		/* Fill the stream buffer if necessary. */
		if (stream->buffer == NULL || stream->error ==
		    MAD_ERROR_BUFLEN) {
			ret = ip_mad_fill_stream(fp, stream, buf, bufsize);
			if (ret == IP_MAD_EOF || ret == IP_MAD_ERROR)
				return ret;
		}

		if (mad_header_decode(header, stream) == -1) {
			/* Error encountered. */
			if (MAD_RECOVERABLE(stream->error) ||
			    stream->error == MAD_ERROR_BUFLEN)
				/* Non-fatal error: try again. */
				continue;
			else {
				/* Fatal error. */
				LOG_ERRX("mad_header_decode: %s",
				    mad_stream_errorstr(stream));
				msg_errx("Cannot decode frame header: %s",
				    mad_stream_errorstr(stream));
				return IP_MAD_ERROR;
			}
		}

		/* Success. */
		return IP_MAD_OK;
	}
}

static int
ip_mad_fill_stream(FILE *fp, struct mad_stream *stream, unsigned char *buf,
    size_t bufsize)
{
	size_t buffree, buflen, nread;

	if (stream->next_frame == NULL)
		buflen = 0;
	else {
		buflen = stream->bufend - stream->next_frame;
		(void)memmove(buf, stream->next_frame, buflen);
	}
	buffree = bufsize - buflen;

	if ((nread = fread(buf + buflen, 1, buffree, fp)) < buffree) {
		if (ferror(fp)) {
			LOG_ERR("fread");
			msg_err("Cannot read from track");
			return IP_MAD_ERROR;
		}
		if (feof(fp)) {
			if (nread == 0)
				return IP_MAD_EOF;

			(void)memset(buf + buflen + nread, 0,
			    MAD_BUFFER_GUARD);
			buflen += MAD_BUFFER_GUARD;
		}
	}

	buflen += nread;
	mad_stream_buffer(stream, buf, buflen);
	stream->error = MAD_ERROR_NONE;
	return IP_MAD_OK;
}

/*
 * Convert a fixed-point number to a 16-bit integer sample.
 */
static int16_t
ip_mad_fixed_to_int(mad_fixed_t fixed)
{
	/* Round to the 15th most significant fraction bit. */
	fixed += (mad_fixed_t)1 << (MAD_F_FRACBITS - 16);

	/* Clip samples equal to or higher than 1.0 or less than -1.0. */
	if (fixed >= MAD_F_ONE)
		fixed = MAD_F_ONE - 1;
	else if (fixed < -MAD_F_ONE)
		fixed = -MAD_F_ONE;

	/* Save the sign bit and the 15 most significant fraction bits. */
	return (int16_t)(fixed >> (MAD_F_FRACBITS - 15));
}

static char *
ip_mad_get_id3_frame(const struct id3_tag *tag, const char *id)
{
	struct id3_frame	*frame;
	union id3_field		*field;
	const id3_ucs4_t	*value;

	/* Note that some libid3tag functions return 0 instead of NULL. */

	if ((frame = id3_tag_findframe(tag, id, 0)) == 0)
		return NULL;

	/*
	 * The first field specifies the text encoding, while the second
	 * contains the actual text.
	 */
	if ((field = id3_frame_field(frame, 1)) == 0)
		return NULL;

	/* Get the first string from the "string list" field. */
	if ((value = id3_field_getstrings(field, 0)) == 0)
		return NULL;

	/*
	 * Note that id3_ucs4_latin1duplicate() returns NULL if its call to
	 * malloc() failed.
	 */
	return (char *)id3_ucs4_latin1duplicate(value);
}

static char *
ip_mad_get_id3_genre(struct id3_tag *tag)
{
	struct id3_frame	*frame;
	union id3_field		*field;
	const id3_ucs4_t	*genre;

	if ((frame = id3_tag_findframe(tag, ID3_FRAME_GENRE, 0)) == 0)
		return NULL;

	if ((field = id3_frame_field(frame, 1)) == 0)
		return NULL;

	genre = id3_genre_name(id3_field_getstrings(field, 0));
	if (genre[0] == '\0')
		return NULL;

	return (char *)id3_ucs4_latin1duplicate(genre);
}

static int
ip_mad_get_metadata(struct track *t)
{
	struct id3_file		*file;
	struct id3_tag		*tag;
	char			*tlen;
	const char		*errstr;

	if ((file = id3_file_open(t->path, ID3_FILE_MODE_READONLY)) == NULL) {
		LOG_ERRX("%s: id3_file_open() failed", t->path);
		msg_errx("%s: Cannot open file", t->path);
		return IP_MAD_ERROR;
	}

	tag = id3_file_tag(file);

	t->album = ip_mad_get_id3_frame(tag, ID3_FRAME_ALBUM);
	t->artist = ip_mad_get_id3_frame(tag, ID3_FRAME_ARTIST);
	t->date = ip_mad_get_id3_frame(tag, ID3_FRAME_YEAR);
	t->title = ip_mad_get_id3_frame(tag, ID3_FRAME_TITLE);
	t->tracknumber = ip_mad_get_id3_frame(tag, ID3_FRAME_TRACK);
	t->genre = ip_mad_get_id3_genre(tag);

	/*
	 * ID3 allows track numbers of the form "x/y". We ignore the slash and
	 * everything after it.
	 */
	if (t->tracknumber != NULL)
		t->tracknumber[strcspn(t->tracknumber, "/")] = '\0';

	if ((tlen = ip_mad_get_id3_frame(tag, "TLEN")) == NULL)
		t->duration = ip_mad_calculate_duration(t->path);
	else {
		t->duration = (unsigned int)strtonum(tlen, 0, UINT_MAX,
		    &errstr);
		if (errstr != NULL)
			LOG_ERRX("%s: %s: TLEN frame is %s", t->path, tlen,
			    errstr);
		else
			t->duration /= 1000;
		free(tlen);
	}

	if (id3_file_close(file) == -1)
		LOG_ERRX("%s: id3_file_close() failed", t->path);

	return 0;
}

static int
ip_mad_get_position(struct track *t, unsigned int *pos)
{
	struct ip_mad_ipdata *ipd;

	ipd = t->ipdata;
	*pos = (unsigned int)mad_timer_count(ipd->timer, MAD_UNITS_SECONDS);
	return 0;
}

static int
ip_mad_get_sample_format(const char *file, struct sample_format *sf,
    unsigned char *buf, size_t bufsize)
{
	FILE			*fp;
	struct mad_stream	 stream;
	struct mad_header	 header;
	int			 ret;

	if ((fp = fopen(file, "r")) == NULL) {
		LOG_ERR("fopen: %s", file);
		msg_err("%s: Cannot open track", file);
		return IP_MAD_ERROR;
	}

	mad_stream_init(&stream);
	mad_header_init(&header);

	ret = ip_mad_decode_frame_header(fp, &stream, &header, buf, bufsize);
	if (ret == IP_MAD_EOF)
		msg_errx("File is empty");
	else if (ret == IP_MAD_OK) {
		sf->nbits = 16;
		sf->nchannels = MAD_NCHANNELS(&header);
		sf->rate = header.samplerate;
	}

	mad_header_finish(&header);
	mad_stream_finish(&stream);
	(void)fclose(fp);

	return ret == IP_MAD_OK ? 0 : -1;
}

static int
ip_mad_open(struct track *t)
{
	struct ip_mad_ipdata	*ipd;
	int			 ret;

	ipd = xmalloc(sizeof *ipd);

	if ((ipd->fp = fopen(t->path, "r")) == NULL) {
		LOG_ERR("fopen: %s", t->path);
		msg_err("%s: Cannot open track", t->path);
		free(ipd);
		return -1;
	}

	t->ipdata = ipd;
	ipd->buf = xmalloc(IP_MAD_BUFSIZE + MAD_BUFFER_GUARD);
	ipd->sampleidx = 0;

	mad_stream_init(&ipd->stream);
	mad_frame_init(&ipd->frame);
	mad_synth_init(&ipd->synth);
	mad_timer_reset(&ipd->timer);

	if ((ret = ip_mad_get_sample_format(t->path, &t->format, ipd->buf,
	    IP_MAD_BUFSIZE))) {
		ip_mad_close(t);
		return ret;
	}

	return 0;
}

static int
ip_mad_read(struct track *t, int16_t *samples, size_t maxsamples)
{
	struct ip_mad_ipdata	*ipd;
	size_t			 nsamples;
	int			 ret;
	unsigned short		 i;

	ipd = t->ipdata;

	nsamples = 0;
	while (nsamples + (size_t)t->format.nchannels <= maxsamples) {
		if (ipd->sampleidx == ipd->synth.pcm.length) {
			mad_timer_add(&ipd->timer, ipd->frame.header.duration);
			ret = ip_mad_decode_frame(ipd);
			if (ret == IP_MAD_EOF)
				break;
			if (ret == IP_MAD_ERROR)
				return ret;
		}

		for (i = 0; i < ipd->synth.pcm.channels; i++)
			samples[nsamples++] = ip_mad_fixed_to_int(
			    ipd->synth.pcm.samples[i][ipd->sampleidx]);

		ipd->sampleidx++;
	}

	return (int)nsamples;
}

static void
ip_mad_seek(struct track *t, unsigned int seekpos)
{
	struct ip_mad_ipdata	*ipd;
	struct mad_header	 header;
	unsigned int		 pos;

	ipd = t->ipdata;

	pos = (unsigned int)mad_timer_count(ipd->timer, MAD_UNITS_SECONDS);
	if (pos > seekpos) {
		if (fseek(ipd->fp, 0, SEEK_SET) == -1) {
			LOG_ERR("fseek: %s", t->path);
			msg_err("Cannot seek");
			return;
		}
		mad_timer_reset(&ipd->timer);
		pos = 0;
	}

	mad_header_init(&header);

	for (;;) {
		if (pos >= seekpos)
			break;
		if (ip_mad_decode_frame_header(ipd->fp, &ipd->stream, &header,
		    ipd->buf, IP_MAD_BUFSIZE) != IP_MAD_OK)
			break;
		mad_timer_add(&ipd->timer, header.duration);
		pos = mad_timer_count(ipd->timer, MAD_UNITS_SECONDS);
	}

	mad_header_finish(&header);
	mad_frame_mute(&ipd->frame);
	mad_synth_mute(&ipd->synth);
}
