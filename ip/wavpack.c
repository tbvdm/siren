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

/*
 * Note that the WavPack API uses the term "sample" to refer to "sample per
 * channel" (i.e. a frame).
 */

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wavpack/wavpack.h>

#include "../siren.h"

#define IP_WAVPACK_BUFSIZE	2048	/* Buffer size, in frames */
#define IP_WAVPACK_ERRSTRLEN	80	/* As per the documentation */

struct ip_wavpack_ipdata {
	WavpackContext	*wpc;
	int		 float_samples;
	int32_t		*buf;
	uint32_t	 bufidx;	/* Current sample */
	uint32_t	 buflen;	/* Buffer length, in samples */
};

static void		 ip_wavpack_close(struct track *);
static void		 ip_wavpack_get_metadata(struct track *);
static int		 ip_wavpack_get_position(struct track *,
			    unsigned int *);
static char		*ip_wavpack_get_tag_item(WavpackContext *,
			    const char *);
static int		 ip_wavpack_open(struct track *);
static int		 ip_wavpack_read(struct track *,
			    struct sample_buffer *);
static void		 ip_wavpack_seek(struct track *, unsigned int);

static const char	*ip_wavpack_extensions[] = { "wv", NULL };

const struct ip		 ip = {
	"wavpack",
	IP_PRIORITY_WAVPACK,
	ip_wavpack_extensions,
	ip_wavpack_close,
	ip_wavpack_get_metadata,
	ip_wavpack_get_position,
	NULL,
	ip_wavpack_open,
	ip_wavpack_read,
	ip_wavpack_seek
};

static void
ip_wavpack_close(struct track *t)
{
	struct ip_wavpack_ipdata *ipd;

	ipd = t->ipdata;
	WavpackCloseFile(ipd->wpc);
	free(ipd->buf);
	free(ipd);
}

static void
ip_wavpack_get_metadata(struct track *t)
{
	WavpackContext	*wpc;
	uint32_t	 nframes, rate;
	char		 errstr[IP_WAVPACK_ERRSTRLEN], *val;

	wpc = WavpackOpenFileInput(t->path, errstr, OPEN_TAGS, 0);
	if (wpc == NULL) {
		LOG_ERRX("WavpackOpenFileInput: %s: %s", t->path, errstr);
		msg_errx("%s: Cannot open track: %s", t->path, errstr);
		return;
	}

	t->album = ip_wavpack_get_tag_item(wpc, "album");
	t->artist = ip_wavpack_get_tag_item(wpc, "artist");
	t->comment = ip_wavpack_get_tag_item(wpc, "comment");
	t->date = ip_wavpack_get_tag_item(wpc, "year");
	t->genre = ip_wavpack_get_tag_item(wpc, "genre");
	t->title = ip_wavpack_get_tag_item(wpc, "title");

	val = ip_wavpack_get_tag_item(wpc, "track");
	if (val != NULL) {
		track_split_tag(val, &t->tracknumber, &t->tracktotal);
		free(val);
	}

	/*
	 * APEv2 doesn't have a standard key for the disc number. Try the
	 * "disc" and "part" keys.
	 */
	val = ip_wavpack_get_tag_item(wpc, "disc");
	if (val == NULL)
		val = ip_wavpack_get_tag_item(wpc, "part");
	if (val != NULL) {
		track_split_tag(val, &t->discnumber, &t->disctotal);
		free(val);
	}

	/*
	 * APEv2 doesn't have a standard key for the album artist. Try the
	 * "albumartist" and "album artist" keys.
	 */
	t->albumartist = ip_wavpack_get_tag_item(wpc, "albumartist");
	if (t->albumartist == NULL)
		t->albumartist = ip_wavpack_get_tag_item(wpc, "album artist");

	nframes = WavpackGetNumSamples(wpc);
	rate = WavpackGetSampleRate(wpc);
	if (nframes == (uint32_t)-1 || rate == 0)
		t->duration = 0;
	else
		t->duration = nframes / rate;

	WavpackCloseFile(wpc);
}

static int
ip_wavpack_get_position(struct track *t, unsigned int *pos)
{
	struct ip_wavpack_ipdata	*ipd;
	uint32_t			 curframe;

	ipd = t->ipdata;

	curframe = WavpackGetSampleIndex(ipd->wpc);
	*pos = curframe / t->format.rate;
	return 0;
}

/*
 * Return the value of the APEv2 or ID3v1 tag item with the specified key.
 */
static char *
ip_wavpack_get_tag_item(WavpackContext *wpc, const char *key)
{
	int	 len;
	char	*value;

	len = WavpackGetTagItem(wpc, key, NULL, 0);
	if (len <= 0)
		value = NULL;
	else {
		if (len < INT_MAX)
			len++;
		value = xmalloc(len);
		WavpackGetTagItem(wpc, key, value, len);
	}

	return value;
}

static int
ip_wavpack_open(struct track *t)
{
	struct ip_wavpack_ipdata	*ipd;
	WavpackContext			*wpc;
	int				 float_samples;
	char				 errstr[IP_WAVPACK_ERRSTRLEN];

	wpc = WavpackOpenFileInput(t->path, errstr, OPEN_NORMALIZE | OPEN_WVC,
	    0);
	if (wpc == NULL) {
		LOG_ERRX("WavpackOpenFileInput: %s: %s", t->path, errstr);
		msg_errx("%s: Cannot open track: %s", t->path, errstr);
		return -1;
	}

	float_samples = WavpackGetMode(wpc) & MODE_FLOAT;
	if (float_samples)
		t->format.nbits = 16;
	else
		/*
		 * WavPack aligns samples whose bit depth is not a multiple of
		 * 8 on the MSB rather than the LSB. Therefore, determine the
		 * bit depth from the number of bytes per sample.
		 */
		t->format.nbits = 8 * WavpackGetBytesPerSample(wpc);

	t->format.nchannels = WavpackGetNumChannels(wpc);
	t->format.rate = WavpackGetSampleRate(wpc);

	ipd = xmalloc(sizeof *ipd);
	ipd->wpc = wpc;
	ipd->float_samples = float_samples;
	ipd->bufidx = 0;
	ipd->buflen = 0;
	ipd->buf = xreallocarray(NULL,
	    IP_WAVPACK_BUFSIZE * t->format.nchannels, sizeof *ipd->buf);

	t->ipdata = ipd;
	return 0;
}

static int
ip_wavpack_read(struct track *t, struct sample_buffer *sb)
{
	struct ip_wavpack_ipdata	*ipd;
	uint32_t			 ret;
	float				 f;

	ipd = t->ipdata;

	for (sb->len_s = 0; sb->len_s < sb->size_s; sb->len_s++) {
		if (ipd->bufidx == ipd->buflen) {
			ret = WavpackUnpackSamples(ipd->wpc, ipd->buf,
			    IP_WAVPACK_BUFSIZE);
			if (ret == 0)
				/* EOF reached. */
				break;
			ipd->buflen = ret * t->format.nchannels;
			ipd->bufidx = 0;
		}

		if (!ipd->float_samples)
			switch (sb->nbytes) {
			case 1:
				sb->data1[sb->len_s] = ipd->buf[ipd->bufidx];
				break;
			case 2:
				sb->data2[sb->len_s] = ipd->buf[ipd->bufidx];
				break;
			case 4:
				sb->data4[sb->len_s] = ipd->buf[ipd->bufidx];
				break;
			}
		else {
			/* We assume floats use IEEE 754 representation. */
			f = *(float *)&ipd->buf[ipd->bufidx] * -INT16_MIN;
			if (f < INT16_MIN)
				sb->data2[sb->len_s] = INT16_MIN;
			else if (f > INT16_MAX)
				sb->data2[sb->len_s] = INT16_MAX;
			else
				sb->data2[sb->len_s] = f;
		}

		ipd->bufidx++;
	}

	sb->len_b = sb->len_s * sb->nbytes;
	return sb->len_s != 0;
}

static void
ip_wavpack_seek(struct track *t, unsigned int sec)
{
	struct ip_wavpack_ipdata	*ipd;
	uint32_t			 frame;

	ipd = t->ipdata;
	frame = sec * t->format.rate;
	if (!WavpackSeekSample(ipd->wpc, frame)) {
		LOG_ERRX("WavpackSeekSample: %s: %s", t->path,
		    WavpackGetErrorMessage(ipd->wpc));
		msg_errx("Cannot seek: %s", WavpackGetErrorMessage(ipd->wpc));
	}
}
