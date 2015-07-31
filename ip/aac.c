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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <mp4ff.h>
#include <neaacdec.h>

#include "../siren.h"

struct ip_aac_config {
	unsigned int		 bufsize;
	unsigned char		*buf;
};

struct ip_aac_file {
	FILE			*fp;
	mp4ff_t			*mf;
	mp4ff_callback_t	 cb;
	int			 track;
};

struct ip_aac_ipdata {
	struct ip_aac_file	 f;
	NeAACDecHandle		 dec;
	int			 nsamples;
	int			 sample;
	int32_t			 timescale;
	unsigned long		 bufsize;
	char			*buf;
};

static void		 ip_aac_close(struct track *);
static void		 ip_aac_get_metadata(struct track *);
static int		 ip_aac_get_position(struct track *, unsigned int *);
static int		 ip_aac_open(struct track *);
static int		 ip_aac_read(struct track *, int16_t *, size_t);
static void		 ip_aac_seek(struct track *, unsigned int);

static const char	*ip_aac_extensions[] = { "aac", "m4a", "mp4", NULL };

const struct ip		 ip = {
	"aac",
	ip_aac_extensions,
	ip_aac_close,
	ip_aac_get_metadata,
	ip_aac_get_position,
	ip_aac_open,
	ip_aac_read,
	ip_aac_seek
};

static uint32_t
ip_aac_read_cb(void *fp, void *buf, uint32_t bufsize)
{
	return fread(buf, 1, bufsize, (FILE *)fp);
}

static uint32_t
ip_aac_seek_cb(void *fp, uint64_t pos)
{
	fseek((FILE *)fp, pos, SEEK_SET);
	return 0; /* libmp4ff doesn't check the return value */
}

static int
ip_aac_get_aac_track(mp4ff_t *mf, struct ip_aac_config *c)
{
	mp4AudioSpecificConfig	asc;
	int			i, ntracks;

	ntracks = mp4ff_total_tracks(mf);
	if (ntracks <= 0)
		return -1;

	for (i = 0; i < ntracks; i++) {
		mp4ff_get_decoder_config(mf, i, &c->buf, &c->bufsize);
		if (c->buf != NULL && NeAACDecAudioSpecificConfig(c->buf,
		    c->bufsize, &asc) == 0)
			return i;
	}

	free(c->buf);
	return -1;
}

static void
ip_aac_close_file(struct ip_aac_file *f)
{
	mp4ff_close(f->mf);
	fclose(f->fp);
}

static int
ip_aac_open_file(const char *path, struct ip_aac_file *f,
    struct ip_aac_config *c)
{
	f->fp = fopen(path, "r");
	if (f->fp == NULL) {
		LOG_ERR("fopen: %s", path);
		msg_err("%s: Cannot open file", path);
		return -1;
	}

	f->cb.read = ip_aac_read_cb;
	f->cb.seek = ip_aac_seek_cb;
	f->cb.user_data = f->fp;
	f->cb.write = NULL;
	f->cb.truncate = NULL;

	/* No need to check return value; see mp4ff_open_read() code. */
	f->mf = mp4ff_open_read(&f->cb);

	f->track = ip_aac_get_aac_track(f->mf, c);
	if (f->track == -1) {
		LOG_ERRX("%s: cannot find AAC track", path);
		msg_errx("%s: Cannot find AAC track", path);
		ip_aac_close_file(f);
		return -1;
	}

	return 0;
}

static int
ip_aac_fill_buffer(struct track *t, struct ip_aac_ipdata *ipd)
{
	NeAACDecFrameInfo	 frame;
	unsigned int		 bufsize;
	unsigned char		*buf;
	char			*errmsg;

	if (ipd->sample == ipd->nsamples)
		return 0; /* EOF reached */

	do {
		if (mp4ff_read_sample(ipd->f.mf, ipd->f.track, ipd->sample++,
		    &buf, &bufsize) == 0) {
			LOG_ERRX("%s: mp4ff_read_sample() failed", t->path);
			msg_errx("Cannot read from file");
			return -1;
		}
		ipd->buf = NeAACDecDecode(ipd->dec, &frame, buf, bufsize);
		free(buf);
		if (frame.error) {
			errmsg = NeAACDecGetErrorMessage(frame.error);
			LOG_ERRX("NeAACDecDecode: %s: %s", t->path, errmsg);
			msg_errx("Cannot read from file: %s", errmsg);
			return -1;
		}
	} while (ipd->buf == NULL || frame.samples == 0);

	ipd->bufsize = frame.samples * 2; /* 16-bit samples */
	return 1;
}

static void
ip_aac_close(struct track *t)
{
	struct ip_aac_ipdata *ipd;

	ipd = t->ipdata;
	NeAACDecClose(ipd->dec);
	ip_aac_close_file(&ipd->f);
}

static void
ip_aac_get_metadata(struct track *t)
{
	struct ip_aac_file	 f;
	struct ip_aac_config	 c;
	int64_t			 duration;
	int32_t			 scale;
	int			 i, nitems;
	char			*name, *value;

	if (ip_aac_open_file(t->path, &f, &c) == -1)
		return;

	free(c.buf);

	nitems = mp4ff_meta_get_num_items(f.mf);
	for (i = 0; i < nitems; i++) {
		mp4ff_meta_get_by_index(f.mf, i, &name, &value);
		if (name == NULL)
			free(value);
		else {
			if (!strcasecmp(name, "album"))
				t->album = value;
			else if (!strcasecmp(name, "artist"))
				t->artist = value;
			else if (!strcasecmp(name, "date"))
				t->date = value;
			else if (!strcasecmp(name, "disc"))
				t->discnumber = value;
			else if (!strcasecmp(name, "genre"))
				t->genre = value;
			else if (!strcasecmp(name, "title"))
				t->title = value;
			else if (!strcasecmp(name, "track"))
				t->tracknumber = value;
			else
				free(value);
			free(name);
		}
	}

	duration = mp4ff_get_track_duration(f.mf, f.track);
	scale = mp4ff_time_scale(f.mf, f.track);
	t->duration = (duration <= 0 || scale <= 0) ? 0 : duration / scale;

	ip_aac_close_file(&f);
}

static int
ip_aac_get_position(struct track *t, unsigned int *pos)
{
	struct ip_aac_ipdata	*ipd;
	int64_t			 sp;

	ipd = t->ipdata;
	sp = mp4ff_get_sample_position(ipd->f.mf, ipd->f.track, ipd->sample);
	if (sp < 0) {
		LOG_ERRX("mp4ff_get_sample_position() failed");
		return -1;
	} else {
		*pos = sp / ipd->timescale;
		return 0;
	}
}

static int
ip_aac_open(struct track *t)
{
	struct ip_aac_ipdata		*ipd;
	struct ip_aac_config		 c;
	NeAACDecConfigurationPtr	 cfg;
	unsigned long			 rate;
	unsigned char			 nchan;

	ipd = xmalloc(sizeof *ipd);

	if (ip_aac_open_file(t->path, &ipd->f, &c) == -1) {
		free(ipd);
		return -1;
	}

	ipd->nsamples = mp4ff_num_samples(ipd->f.mf, ipd->f.track);
	ipd->sample = 0;
	ipd->buf = NULL;
	ipd->bufsize = 0;

	ipd->timescale = mp4ff_time_scale(ipd->f.mf, ipd->f.track);
	if (ipd->timescale <= 0) {
		LOG_ERRX("%d: invalid timescale", ipd->timescale);
		goto error1;
	}

	ipd->dec = NeAACDecOpen();
	if (ipd->dec == NULL) {
		LOG_ERRX("%s: NeAACDecOpen() failed", t->path);
		goto error1;
	}

	cfg = NeAACDecGetCurrentConfiguration(ipd->dec);
	cfg->outputFormat = FAAD_FMT_16BIT;
	cfg->downMatrix = 1; /* Down-matrix 5.1 channels to 2 */
	if (NeAACDecSetConfiguration(ipd->dec, cfg) != 1) {
		LOG_ERRX("%s: NeAACDecSetConfiguration() failed", t->path);
		goto error2;
	}

	if (NeAACDecInit2(ipd->dec, c.buf, c.bufsize, &rate, &nchan) != 0) {
		LOG_ERRX("%s: NeAACDecInit2() failed", t->path);
		goto error2;
	}
	free (c.buf);

	t->format.nbits = 16;
	t->format.nchannels = nchan;
	t->format.rate = rate;
	t->ipdata = ipd;
	return 0;

error2:
	NeAACDecClose(ipd->dec);

error1:
	msg_errx("%s: Cannot open file", t->path);
	ip_aac_close_file(&ipd->f);
	free(ipd);
	free(c.buf);
	return -1;
}

static int
ip_aac_read(struct track *t, int16_t *samples, size_t maxsamples)
{
	struct ip_aac_ipdata	*ipd;
	char			*obuf;
	size_t			 len, obufsize;
	int			 ret;

	ipd = t->ipdata;
	obuf = (char *)samples;
	obufsize = maxsamples * 2; /* 16-bit samples */

	while (obufsize) {
		if (ipd->bufsize == 0) {
			ret = ip_aac_fill_buffer(t, ipd);
			if (ret <= 0)
				return ret; /* EOF or error */
		}
		len = (obufsize < ipd->bufsize) ? obufsize : ipd->bufsize;
		memcpy(obuf, ipd->buf, len);
		obuf += len;
		obufsize -= len;
		ipd->buf += len;
		ipd->bufsize -= len;
	}

	/* Return number of (16-bit) samples read. */
	return maxsamples - (obufsize / 2);
}

static void
ip_aac_seek(struct track *t, unsigned int pos)
{
	struct ip_aac_ipdata	*ipd;
	int			 s;

	ipd = t->ipdata;
	s = mp4ff_find_sample(ipd->f.mf, ipd->f.track, pos * ipd->timescale,
	    NULL);
	if (s < 0)
		LOG_ERRX("mp4ff_find_sample() failed");
	else
		ipd->sample = s;
}
