/*
 * Copyright (c) 2016 Tim van der Molen <tim@kariliq.nl>
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libavformat/avformat.h>

#include "../siren.h"

#define IP_FFMPEG_ERROR	-1
#define IP_FFMPEG_EOF	0
#define IP_FFMPEG_OK	1

/* Libav does not have av_err2str() */
#ifdef av_err2str
#define IP_FFMPEG_LOG(str) \
    LOG_ERRX("%s: %s: %s", t->path, str, av_err2str(ret))
#define IP_FFMPEG_MSG(str) \
    msg_errx("%s: %s: %s", t->path, str, av_err2str(ret))
#else
#define IP_FFMPEG_LOG(str) LOG_ERRX("%s: %s", t->path, str)
#define IP_FFMPEG_MSG(str) msg_errx("%s: %s", t->path, str)
#endif

struct ip_ffmpeg_ipdata {
	AVFormatContext	*fmtctx;
	AVCodecContext	*codecctx;
	AVPacket	 packet;
	AVFrame		*frame;
	int64_t		 timestamp;
	int		 stream;	/* Audio stream index		*/
	int		 pdatalen;	/* Remaining packet data length	*/
	int		 fdatalen;	/* Remaining frame data length	*/
	uint8_t		*fdata;		/* Remaining frame data		*/
	int		 sample;	/* Sample index in frame	*/
};

static void		 ip_ffmpeg_close(struct track *);
static void		 ip_ffmpeg_get_metadata(struct track *);
static int		 ip_ffmpeg_get_position(struct track *,
			    unsigned int *);
static int		 ip_ffmpeg_init(void);
static int		 ip_ffmpeg_open(struct track *);
static int		 ip_ffmpeg_read(struct track *,
			    struct sample_buffer *);
static void		 ip_ffmpeg_seek(struct track *, unsigned int);

static const char	*ip_ffmpeg_extensions[] = { NULL };

const struct ip		 ip = {
	"ffmpeg",
	ip_ffmpeg_extensions,
	ip_ffmpeg_close,
	ip_ffmpeg_get_metadata,
	ip_ffmpeg_get_position,
	ip_ffmpeg_init,
	ip_ffmpeg_open,
	ip_ffmpeg_read,
	ip_ffmpeg_seek
};

static void
ip_ffmpeg_log(UNUSED void *p, int level, const char *fmt, va_list ap)
{
	if (level <= AV_LOG_WARNING)
		LOG_VERRX(fmt, ap);
}

/*
 * Read the next packet from the audio stream (i.e. skip packets from other
 * streams)
 */
static int
ip_ffmpeg_read_packet(struct track *t, struct ip_ffmpeg_ipdata *ipd)
{
	int ret;

	for (;;) {
		/* Free the previous packet */
		av_packet_unref(&ipd->packet);

		ret = av_read_frame(ipd->fmtctx, &ipd->packet);
		if (ret == AVERROR_EOF)
			return IP_FFMPEG_EOF;
		if (ret < 0) {
			IP_FFMPEG_LOG("av_read_frame");
			IP_FFMPEG_MSG("Cannot read from file");
			return IP_FFMPEG_ERROR;
		}
		if (ipd->packet.stream_index == ipd->stream) {
			ipd->pdatalen = ipd->packet.size;
			ipd->timestamp = ipd->packet.pts;
			return IP_FFMPEG_OK;
		}
	}
}

/*
 * Decode the next frame in a packet
 */
static int
ip_ffmpeg_decode_frame(struct track *t, struct ip_ffmpeg_ipdata *ipd)
{
	int eof, got_frame, ret;

	eof = 0;
	for (;;) {
		if (ipd->pdatalen == 0) {
			ret = ip_ffmpeg_read_packet(t, ipd);
			if (ret == IP_FFMPEG_ERROR)
				return ret;
			if (ret == IP_FFMPEG_EOF) {
				/* Flush the decoder */
				eof = 1;
				ipd->packet.data = NULL;
				ipd->packet.size = 0;
			}
		}

		ret = avcodec_decode_audio4(ipd->codecctx, ipd->frame,
		    &got_frame, &ipd->packet);
		if (ret < 0) {
			IP_FFMPEG_LOG("avcodec_decode_audio4");
			IP_FFMPEG_MSG("Decoding error");
			return IP_FFMPEG_ERROR;
		}
		ipd->pdatalen -= ret;

		if (got_frame) {
			ret = av_samples_get_buffer_size(NULL,
			    ipd->codecctx->channels, ipd->frame->nb_samples,
			    ipd->codecctx->sample_fmt, 1);
			if (ret < 0) {
				IP_FFMPEG_LOG("av_samples_get_buffer_size");
				IP_FFMPEG_MSG("Decoding error");
				return IP_FFMPEG_ERROR;
			}

			ipd->fdatalen = ret;
			ipd->fdata = ipd->frame->data[0];
			return IP_FFMPEG_OK;
		}

		if (eof)
			/* No more frames, so the decoder has been flushed */
			return IP_FFMPEG_EOF;
	}
}

static int
ip_ffmpeg_read_interleaved(struct track *t, struct ip_ffmpeg_ipdata *ipd,
    struct sample_buffer *sb)
{
	char	*buf;
	size_t	 bufsize, len;
	int	 ret;

	ipd = t->ipdata;
	buf = (char *)sb->data;
	bufsize = sb->size_b;

	while (bufsize > 0) {
		if (ipd->fdatalen == 0) {
			ret = ip_ffmpeg_decode_frame(t, ipd);
			if (ret == IP_FFMPEG_EOF)
				break;
			if (ret == IP_FFMPEG_ERROR)
				return -1;
		}

		switch (ipd->codecctx->sample_fmt) {
		case AV_SAMPLE_FMT_S16:
		case AV_SAMPLE_FMT_S32:
			len = (bufsize < (size_t)ipd->fdatalen) ? bufsize :
			    (size_t)ipd->fdatalen;
			memcpy(buf, ipd->fdata, len);
			buf += len;
			bufsize -= len;
			ipd->fdata += len;
			ipd->fdatalen -= len;
			break;
		case AV_SAMPLE_FMT_FLT:
			/* TODO */
			break;
		case AV_SAMPLE_FMT_DBL:
			/* TODO */
			break;
		default:
			break;
		}
	}

	sb->len_b = sb->size_b - bufsize;
	sb->len_s = sb->len_b / sb->nbytes;
	return sb->len_s != 0;
}

static int
ip_ffmpeg_read_planar(struct track *t, struct ip_ffmpeg_ipdata *ipd,
    struct sample_buffer *sb)
{
	double		  d, **data_dbl;
	float		  f, **data_flt;
	int32_t		**data_s32;
	int16_t		**data_s16;
	size_t		  i;
	unsigned int	  j;
	int		  ret;

	ipd = t->ipdata;

	i = 0;
	while (i + t->format.nchannels <= sb->size_s) {
		if (ipd->sample == ipd->frame->nb_samples) {
			ipd->sample = 0;
			ret = ip_ffmpeg_decode_frame(t, ipd);
			if (ret == IP_FFMPEG_EOF)
				break;
			if (ret == IP_FFMPEG_ERROR)
				return -1;
		}

		switch (ipd->codecctx->sample_fmt) {
		case AV_SAMPLE_FMT_S16P:
			data_s16 = (int16_t **)ipd->frame->extended_data;
			for (j = 0; j < t->format.nchannels; j++)
				sb->data2[i++] = data_s16[j][ipd->sample];
			break;
		case AV_SAMPLE_FMT_S32P:
			data_s32 = (int32_t **)ipd->frame->extended_data;
			for (j = 0; j < t->format.nchannels; j++)
				sb->data4[i++] = data_s32[j][ipd->sample];
			break;
		case AV_SAMPLE_FMT_FLTP:
			/* XXX Assuming float is 32-bit */
			data_flt = (float **)ipd->frame->extended_data;
			for (j = 0; j < t->format.nchannels; j++) {
				f = data_flt[j][ipd->sample];
				if (f < -1.0f)
					sb->data2[i++] = INT16_MIN;
				else if (f > 1.0f)
					sb->data2[i++] = INT16_MAX;
				else
					sb->data2[i++] = f * INT16_MAX;
			}
			break;
		case AV_SAMPLE_FMT_DBLP:
			/* XXX Assuming double is 64-bit */
			data_dbl = (double **)ipd->frame->extended_data;
			for (j = 0; j < t->format.nchannels; j++) {
				d = data_dbl[j][ipd->sample];
				if (d < -1.0)
					sb->data2[i++] = INT16_MIN;
				else if (d > 1.0)
					sb->data2[i++] = INT16_MAX;
				else
					sb->data2[i++] = d * INT16_MAX;
			}
			break;
		default:
			break;
		}

		ipd->sample++;
	}

	sb->len_s = i;
	sb->len_b = sb->len_s * sb->nbytes;
	return sb->len_s != 0;
}

static void
ip_ffmpeg_parse_metadata(struct track *t, AVDictionary *metadata)
{
	AVDictionaryEntry	*tag;
	char			*number, *total;

	/*
	 * XXX libavformat/avformat.h: "metadata exported by demuxers isn't
	 * checked to be valid UTF-8 in most cases"
	 */

	number = NULL;
	total = NULL;
	tag = NULL;
	while ((tag = av_dict_get(metadata, "", tag, AV_DICT_IGNORE_SUFFIX)) !=
	    NULL) {
		if (!strcasecmp(tag->key, "album")) {
			free(t->album);
			t->album = xstrdup(tag->value);
		} else if (!strcasecmp(tag->key, "album_artist")) {
			free(t->albumartist);
			t->albumartist = xstrdup(tag->value);
		} else if (!strcasecmp(tag->key, "artist")) {
			free(t->artist);
			t->artist = xstrdup(tag->value);
		} else if (!strcasecmp(tag->key, "comment")) {
			free(t->comment);
			t->comment = xstrdup(tag->value);
		} else if (!strcasecmp(tag->key, "date")) {
			free(t->date);
			t->date = xstrdup(tag->value);
		} else if (!strcasecmp(tag->key, "disc")) {
			track_split_tag(tag->value, &number, &total);
			if (number != NULL) {
				free(t->discnumber);
				t->discnumber = number;
			}
			if (total != NULL) {
				free(t->disctotal);
				t->disctotal = total;
			}
		} else if (!strcasecmp(tag->key, "genre")) {
			free(t->genre);
			t->genre = xstrdup(tag->value);
		} else if (!strcasecmp(tag->key, "title")) {
			free(t->title);
			t->title = xstrdup(tag->value);
		} else if (!strcasecmp(tag->key, "track")) {
			track_split_tag(tag->value, &number, &total);
			if (number != NULL) {
				free(t->tracknumber);
				t->tracknumber = number;
			}
			if (total != NULL) {
				free(t->tracktotal);
				t->tracktotal = total;
			}
		}
	}
}

static void
ip_ffmpeg_close(struct track *t)
{
	struct ip_ffmpeg_ipdata *ipd;

	ipd = t->ipdata;
	av_frame_free(&ipd->frame);
	avcodec_close(ipd->codecctx);
	avformat_close_input(&ipd->fmtctx);
	free(ipd);
}

static void
ip_ffmpeg_get_metadata(struct track *t)
{
	AVFormatContext	*ctx;
	int		 ret;

	ctx = NULL;
	ret = avformat_open_input(&ctx, t->path, NULL, NULL);
	if (ret != 0) {
		IP_FFMPEG_LOG("avformat_open_input");
		IP_FFMPEG_MSG("Cannot open file");
		return;
	}

	/* Sometimes necessary to get the duration */
	ret = avformat_find_stream_info(ctx, NULL);
	if (ret < 0)
		IP_FFMPEG_LOG("avformat_find_stream_info");

	/*
	 * The metadata could be in the format context or they could be in a
	 * stream. Try both.
	 */

	ip_ffmpeg_parse_metadata(t, ctx->metadata);

	ret = av_find_best_stream(ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (ret < 0)
		IP_FFMPEG_LOG("av_find_best_stream");
	else
		ip_ffmpeg_parse_metadata(t, ctx->streams[ret]->metadata);

	if (ctx->duration > 0)
		t->duration = ctx->duration / AV_TIME_BASE;

	avformat_close_input(&ctx);
}

static int
ip_ffmpeg_get_position(struct track *t, unsigned int *pos)
{
	struct ip_ffmpeg_ipdata *ipd;

	ipd = t->ipdata;
	*pos = ipd->timestamp *
	    ipd->fmtctx->streams[ipd->stream]->time_base.num /
	    ipd->fmtctx->streams[ipd->stream]->time_base.den;
	return 0;
}

static int
ip_ffmpeg_init(void)
{
	av_log_set_callback(ip_ffmpeg_log);
	av_register_all();
	return 0;
}

static int
ip_ffmpeg_open(struct track *t)
{
	struct ip_ffmpeg_ipdata	*ipd;
	AVCodec			*codec;
	int			 ret;

	ipd = xmalloc(sizeof *ipd);

	ipd->fmtctx = NULL;
	ret = avformat_open_input(&ipd->fmtctx, t->path, NULL, NULL);
	if (ret != 0) {
		IP_FFMPEG_LOG("avformat_open_input");
		IP_FFMPEG_MSG("Cannot open file");
		goto error1;
	}

	ret = avformat_find_stream_info(ipd->fmtctx, NULL);
	if (ret < 0) {
		IP_FFMPEG_LOG("avformat_find_stream_info");
		IP_FFMPEG_MSG("Cannot open file");
		goto error2;
	}

	ret = av_find_best_stream(ipd->fmtctx, AVMEDIA_TYPE_AUDIO, -1, -1,
	    NULL, 0);
	if (ret < 0) {
		IP_FFMPEG_LOG("av_find_best_stream");
		IP_FFMPEG_MSG("Cannot open file");
		goto error2;
	}
	ipd->stream = ret;

	ipd->codecctx = ipd->fmtctx->streams[ipd->stream]->codec;
	codec = avcodec_find_decoder(ipd->codecctx->codec_id);
	if (codec == NULL) {
		LOG_ERRX("%s: cannot find decoder", t->path);
		msg_errx("%s: Cannot find decoder", t->path);
		goto error2;
	}

	ret = avcodec_open2(ipd->codecctx, codec, NULL);
	if (ret != 0) {
		IP_FFMPEG_LOG("avcodec_open2() failed");
		IP_FFMPEG_MSG("Cannot open file");
		goto error2;
	}

	switch (ipd->codecctx->sample_fmt) {
	case AV_SAMPLE_FMT_S16:
	case AV_SAMPLE_FMT_S16P:
	case AV_SAMPLE_FMT_DBLP:
	case AV_SAMPLE_FMT_FLTP:
		t->format.nbits = 16;
		break;
	case AV_SAMPLE_FMT_S32:
	case AV_SAMPLE_FMT_S32P:
		t->format.nbits = 32;
		break;
	default:
		LOG_ERRX("%s: %s: unsupported sample format", t->path,
		    av_get_sample_fmt_name(ipd->codecctx->sample_fmt));
		msg_errx("%s: Unsupported sample format", t->path);
		goto error2;
	}

	av_init_packet(&ipd->packet);
	ipd->packet.size = 0;
	ipd->frame = av_frame_alloc();
	ipd->timestamp = 0;
	ipd->pdatalen = 0;
	ipd->fdatalen = 0;
	ipd->sample = 0;

	t->format.nchannels = ipd->codecctx->channels;
	t->format.rate = ipd->codecctx->sample_rate;
	t->ipdata = ipd;

	return 0;

error2:
	avformat_close_input(&ipd->fmtctx);
error1:
	free(ipd);
	return -1;
}

static int
ip_ffmpeg_read(struct track *t, struct sample_buffer *sb)
{
	struct ip_ffmpeg_ipdata *ipd;

	ipd = t->ipdata;
	if (av_sample_fmt_is_planar(ipd->codecctx->sample_fmt))
		return ip_ffmpeg_read_planar(t, ipd, sb);
	else
		return ip_ffmpeg_read_interleaved(t, ipd, sb);
}

static void
ip_ffmpeg_seek(struct track *t, unsigned int sec)
{
	struct ip_ffmpeg_ipdata	*ipd;
	int64_t			 timestamp;

	ipd = t->ipdata;
	timestamp = sec *
	    ipd->fmtctx->streams[ipd->stream]->time_base.den /
	    ipd->fmtctx->streams[ipd->stream]->time_base.num;
	if (av_seek_frame(ipd->fmtctx, ipd->stream, timestamp, 0) >= 0) {
		/* Flush */
		ipd->pdatalen = 0;
		ipd->fdatalen = 0;
		ipd->sample = ipd->frame->nb_samples;
		avcodec_flush_buffers(ipd->codecctx);
	}
}
