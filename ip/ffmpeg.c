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

#include "../config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libavformat/avformat.h>

#include "../siren.h"

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 33, 100)
#define IP_FFMPEG_AVSTREAM_CODEC_DEPRECATED
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 37, 100)
#define IP_FFMPEG_AVCODEC_DECODE_AUDIO4_DEPRECATED
#endif

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(58, 9, 100)
#define IP_FFMPEG_AV_REGISTER_ALL_DEPRECATED
#endif

#define IP_FFMPEG_ERROR	-1
#define IP_FFMPEG_EOF	0
#define IP_FFMPEG_OK	1

#define IP_FFMPEG_LOG(str) \
    LOG_ERRX("%s: %s: %s", t->path, str, av_err2str(ret))
#define IP_FFMPEG_MSG(str) \
    msg_errx("%s: %s: %s", t->path, str, av_err2str(ret))

struct ip_ffmpeg_ipdata {
	AVFormatContext	*fmtctx;
	AVCodecContext	*codecctx;
	AVPacket	*packet;
	AVFrame		*frame;
	int64_t		 timestamp;
	int		 stream;	/* Audio stream index		*/
#ifdef IP_FFMPEG_AVCODEC_DECODE_AUDIO4_DEPRECATED
	int		 have_packet;
#else
	int		 pdatalen;	/* Remaining packet data length	*/
#endif
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

static const char	*ip_ffmpeg_extensions[] = {
	"8svx", "iff", "svx",		/* Commodore Amiga IFF/8SVX */
	"aa",				/* Audible */
	"aac",
	"ac3",				/* Dolby AC-3 */
	"aif", "aiff",
	"ape",				/* Monkey's Audio */
	"asf", "wma", "wmv",		/* Windows Media Audio */
	"au", "snd",			/* NeXT/Sun audio */
	"avi",
	"avr",				/* Audio Visual Research */
	"caf",				/* Apple Core Audio Format */
	"flac",
	"flv",				/* Flash Video */
	"m4a", "m4b", "mp4",
	"mka", "mkv",			/* Matroska */
	"mp+", "mpc", "mpp",		/* Musepack */
	"mp1",
	"mp2",
	"mp3",
	"mpeg", "mpg",
	"nist", "sph",			/* NIST/Sphere WAVE */
	"oga", "ogg",
	"opus",
	"paf",				/* Ensoniq PARIS audio file */
	"pvf",				/* Portable Voice Format */
	"ra", "ram", "rm", "rv",	/* RealAudio */
	"sf",				/* IRCAM SF */
	"shn",				/* Shorten */
	"tta",				/* True Audio */
	"voc",				/* Create Sound Blaster voice */
	"w64",				/* Sony Sound Forge Wave64 */
	"wav", "wave",
	"webm",
	"wv",				/* WavPack */
	NULL
};

const struct ip		 ip = {
	"ffmpeg",
	IP_PRIORITY_FFMPEG,
	ip_ffmpeg_extensions,
	ip_ffmpeg_close,
	ip_ffmpeg_get_metadata,
	ip_ffmpeg_get_position,
	ip_ffmpeg_init,
	ip_ffmpeg_open,
	ip_ffmpeg_read,
	ip_ffmpeg_seek
};

VPRINTFLIKE3 static void
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
		ret = av_read_frame(ipd->fmtctx, ipd->packet);
		if (ret == AVERROR_EOF) {
			/* Prepare a flush packet */
			ipd->packet->data = NULL;
			ipd->packet->size = 0;
			return IP_FFMPEG_EOF;
		}
		if (ret < 0) {
			IP_FFMPEG_LOG("av_read_frame");
			IP_FFMPEG_MSG("Cannot read from file");
			return IP_FFMPEG_ERROR;
		}
		if (ipd->packet->stream_index == ipd->stream)
			return IP_FFMPEG_OK;

		/* Packet from wrong stream; unref it and try again */
		av_packet_unref(ipd->packet);
	}
}

/*
 * Decode the next frame in a packet
 */
static int
ip_ffmpeg_decode_frame(struct track *t, struct ip_ffmpeg_ipdata *ipd)
{
#ifdef IP_FFMPEG_AVCODEC_DECODE_AUDIO4_DEPRECATED
	int ret;

	for (;;) {
		if (!ipd->have_packet) {
			if (ip_ffmpeg_read_packet(t, ipd) == IP_FFMPEG_ERROR)
				return IP_FFMPEG_ERROR;
			ipd->have_packet = 1;
		}

		ret = avcodec_send_packet(ipd->codecctx, ipd->packet);
		if (ret != AVERROR(EAGAIN)) {
			av_packet_unref(ipd->packet);
			ipd->have_packet = 0;
			if (ret < 0 && ret != AVERROR_EOF) {
				IP_FFMPEG_LOG("avcodec_send_packet");
				IP_FFMPEG_MSG("Decoding error");
				return IP_FFMPEG_ERROR;
			}
		}

		ret = avcodec_receive_frame(ipd->codecctx, ipd->frame);
		if (ret == AVERROR_EOF)
			return IP_FFMPEG_EOF;
		if (ret < 0 && ret != AVERROR(EAGAIN)) {
			IP_FFMPEG_LOG("avcodec_receive_frame");
			IP_FFMPEG_MSG("Decoding error");
			return IP_FFMPEG_ERROR;
		}
		if (ret == 0) {
			ipd->timestamp = ipd->frame->pts;
			return IP_FFMPEG_OK;
		}
	}
#else
	int eof, got_frame, ret;

	eof = 0;
	for (;;) {
		if (ipd->pdatalen == 0) {
			/* Free the previous packet */
			av_packet_unref(ipd->packet);

			ret = ip_ffmpeg_read_packet(t, ipd);
			if (ret == IP_FFMPEG_ERROR)
				return ret;
			if (ret == IP_FFMPEG_EOF)
				eof = 1;

			ipd->pdatalen = ipd->packet->size;
		}

		ret = avcodec_decode_audio4(ipd->codecctx, ipd->frame,
		    &got_frame, ipd->packet);
		if (ret < 0) {
			IP_FFMPEG_LOG("avcodec_decode_audio4");
			IP_FFMPEG_MSG("Decoding error");
			return IP_FFMPEG_ERROR;
		}
		ipd->pdatalen -= ret;

		if (got_frame) {
			ipd->timestamp = ipd->frame->pts;
			return IP_FFMPEG_OK;
		}

		if (eof)
			/* No more frames, so the decoder has been flushed */
			return IP_FFMPEG_EOF;
	}
#endif
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

			ret = av_samples_get_buffer_size(NULL,
			    ipd->codecctx->channels, ipd->frame->nb_samples,
			    ipd->codecctx->sample_fmt, 1);
			if (ret < 0) {
				IP_FFMPEG_LOG("av_samples_get_buffer_size");
				IP_FFMPEG_MSG("Decoding error");
				return -1;
			}

			ipd->fdatalen = ret;
			ipd->fdata = ipd->frame->data[0];
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

	tag = NULL;
	while ((tag = av_dict_get(metadata, "", tag, AV_DICT_IGNORE_SUFFIX)) !=
	    NULL) {
		number = NULL;
		total = NULL;
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
	av_packet_free(&ipd->packet);
	avcodec_close(ipd->codecctx);
#ifdef IP_FFMPEG_AVSTREAM_CODEC_DEPRECATED
	avcodec_free_context(&ipd->codecctx);
#endif
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
#ifndef IP_FFMPEG_AV_REGISTER_ALL_DEPRECATED
	av_register_all();
#endif
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
	ipd->codecctx = NULL;
	ipd->packet = NULL;
	ipd->frame = NULL;
	ipd->timestamp = 0;
#ifdef IP_FFMPEG_AVCODEC_DECODE_AUDIO4_DEPRECATED
	ipd->have_packet = 0;
#else
	ipd->pdatalen = 0;
#endif
	ipd->fdatalen = 0;
	ipd->sample = 0;

	ret = avformat_open_input(&ipd->fmtctx, t->path, NULL, NULL);
	if (ret != 0) {
		IP_FFMPEG_LOG("avformat_open_input");
		IP_FFMPEG_MSG("Cannot open file");
		ipd->fmtctx = NULL;
		goto error;
	}

	ret = avformat_find_stream_info(ipd->fmtctx, NULL);
	if (ret < 0) {
		IP_FFMPEG_LOG("avformat_find_stream_info");
		IP_FFMPEG_MSG("Cannot get stream information");
		goto error;
	}

	ret = av_find_best_stream(ipd->fmtctx, AVMEDIA_TYPE_AUDIO, -1, -1,
	    NULL, 0);
	if (ret < 0) {
		IP_FFMPEG_LOG("av_find_best_stream");
		IP_FFMPEG_MSG("Cannot find audio stream");
		goto error;
	}
	ipd->stream = ret;

#ifdef IP_FFMPEG_AVSTREAM_CODEC_DEPRECATED
	codec = avcodec_find_decoder(
	    ipd->fmtctx->streams[ipd->stream]->codecpar->codec_id);
#else
	ipd->codecctx = ipd->fmtctx->streams[ipd->stream]->codec;
	codec = avcodec_find_decoder(ipd->codecctx->codec_id);
#endif
	if (codec == NULL) {
		LOG_ERRX("%s: avcodec_find_decoder() failed", t->path);
		msg_errx("%s: Cannot find decoder", t->path);
		goto error;
	}

#ifdef IP_FFMPEG_AVSTREAM_CODEC_DEPRECATED
	ipd->codecctx = avcodec_alloc_context3(codec);
	if (ipd->codecctx == NULL) {
		LOG_ERRX("%s: avcodec_allocate_context3() failed", t->path);
		msg_errx("%s: Cannot allocate codec context", t->path);
		goto error;
	}

	ret = avcodec_parameters_to_context(ipd->codecctx,
	    ipd->fmtctx->streams[ipd->stream]->codecpar);
	if (ret < 0) {
		IP_FFMPEG_LOG("avcodec_parameters_to_context");
		IP_FFMPEG_MSG("Cannot copy codec parameters");
		goto error;
	}
#endif

	ret = avcodec_open2(ipd->codecctx, codec, NULL);
	if (ret != 0) {
		IP_FFMPEG_LOG("avcodec_open2");
		IP_FFMPEG_MSG("Cannot initialise codec context");
		goto error;
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
		goto error;
	}

	t->format.nchannels = ipd->codecctx->channels;
	t->format.rate = ipd->codecctx->sample_rate;

	ipd->packet = av_packet_alloc();
	if (ipd->packet == NULL) {
		LOG_ERRX("%s: av_packet_alloc() failed", t->path);
		msg_errx("%s: Cannot allocate packet", t->path);
		goto error;
	}

	ipd->frame = av_frame_alloc();
	if (ipd->frame == NULL) {
		LOG_ERRX("%s: av_frame_alloc() failed", t->path);
		msg_errx("%s: Cannot allocate frame", t->path);
		goto error;
	}

	t->ipdata = ipd;
	return 0;

error:
	if (ipd->packet != NULL)
		av_packet_free(&ipd->packet);
#ifdef IP_FFMPEG_AVSTREAM_CODEC_DEPRECATED
	if (ipd->codecctx != NULL)
		avcodec_free_context(&ipd->codecctx);
#endif
	if (ipd->fmtctx != NULL)
		avformat_close_input(&ipd->fmtctx);
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
#ifdef IP_FFMPEG_AVCODEC_DECODE_AUDIO4_DEPRECATED
		ipd->have_packet = 0;
#else
		ipd->pdatalen = 0;
#endif
		ipd->fdatalen = 0;
		ipd->sample = ipd->frame->nb_samples;
		avcodec_flush_buffers(ipd->codecctx);
	}
}
