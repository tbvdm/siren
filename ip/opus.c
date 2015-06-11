/*
 * Copyright (c) 2015 Tim van der Molen <tbvdm@xs4all.nl>
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
#include <string.h>

#include <opusfile.h>

#include "../siren.h"

#define IP_OPUS_RATE	48000

static void		 ip_opus_close(struct track *);
static void		 ip_opus_get_metadata(struct track *);
static int		 ip_opus_get_position(struct track *, unsigned int *);
static int		 ip_opus_open(struct track *);
static int		 ip_opus_read(struct track *, int16_t *, size_t);
static void		 ip_opus_seek(struct track *, unsigned int);

static const char	*ip_opus_extensions[] = { "opus", NULL };

const struct ip		 ip = {
	"opus",
	ip_opus_extensions,
	ip_opus_close,
	ip_opus_get_metadata,
	ip_opus_get_position,
	ip_opus_open,
	ip_opus_read,
	ip_opus_seek
};

static void
ip_opus_close(struct track *t)
{
	OggOpusFile *oof;

	oof = t->ipdata;
	op_free(oof);
}

static void
ip_opus_get_metadata(struct track *t)
{
	OggOpusFile	 *oof;
	const OpusTags	 *tags;
	int		  error;
	char		**c;

	oof = op_open_file(t->path, &error);
	if (oof == NULL) {
		LOG_ERRX("op_open_file: %s: error %d", t->path, error);
		msg_errx("%s: Cannot open track", t->path);
		return;
	}

	tags = op_tags(oof, -1);
	if (tags != NULL)
		for (c = tags->user_comments; *c != NULL; c++)
			if (!strncasecmp(*c, "album=", 6)) {
				free(t->album);
				t->album = xstrdup(*c + 6);
			} else if (!strncasecmp(*c, "artist=", 7)) {
				free(t->artist);
				t->artist = xstrdup(*c + 7);
			} else if (!strncasecmp(*c, "date=", 5)) {
				free(t->date);
				t->date = xstrdup(*c + 5);
			} else if (!strncasecmp(*c, "genre=", 6)) {
				free(t->genre);
				t->genre = xstrdup(*c + 6);
			} else if (!strncasecmp(*c, "title=", 6)) {
				free(t->title);
				t->title = xstrdup(*c + 6);
			} else if (!strncasecmp(*c, "tracknumber=", 12)) {
				free(t->tracknumber);
				t->tracknumber = xstrdup(*c + 12);
			}

	t->duration = op_pcm_total(oof, -1) / IP_OPUS_RATE;

	op_free(oof);
}

static int
ip_opus_get_position(struct track *t, unsigned int *pos)
{
	OggOpusFile	*oof;
	ogg_int64_t	 offset;

	oof = t->ipdata;
	offset = op_pcm_tell(oof);
	if (offset < 0) {
		LOG_ERRX("op_pcm_tell: %s: error %lld", t->path, offset);
		msg_errx("Cannot get track position");
		return -1;
	}

	*pos = offset / IP_OPUS_RATE;
	return 0;
}

static int
ip_opus_open(struct track *t)
{
	OggOpusFile	*oof;
	int		 error;

	oof = op_open_file(t->path, &error);
	if (oof == NULL) {
		LOG_ERRX("op_open_file: %s: error %d", t->path, error);
		msg_errx("%s: Cannot open track", t->path);
		return -1;
	}

	t->format.nbits = 16;
	t->format.nchannels = op_channel_count(oof, -1);
	t->format.rate = IP_OPUS_RATE;

	t->ipdata = oof;
	return 0;
}

static int
ip_opus_read(struct track *t, int16_t *samples, size_t maxsamples)
{
	OggOpusFile	*oof;
	size_t		 n;
	int		 ret;

	oof = t->ipdata;
	n = 0;

	for (;;) {
		ret = op_read(oof, samples + n, maxsamples - n, NULL);
		if (ret == OP_HOLE) {
			LOG_ERRX("op_read: %s: hole in data", t->path);
		} else if (ret < 0) {
			LOG_ERRX("op_read: %s: error %d", t->path, ret);
			msg_errx("Cannot read from track");
			return -1;
		} else if (ret == 0) {
			return n;
		} else {
			n += ret * op_channel_count(oof, -1);
			if (n == maxsamples)
				return n;
		}
	}
}

static void
ip_opus_seek(struct track *t, unsigned int sec)
{
	OggOpusFile	*oof;
	int		 ret;

	oof = t->ipdata;
	ret = op_pcm_seek(oof, sec * IP_OPUS_RATE);
	if (ret < 0) {
		LOG_ERRX("op_pcm_seek: %s: error %d", t->path, ret);
		msg_errx("Cannot seek");
	}
}
