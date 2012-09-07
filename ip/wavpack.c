/*
 * Copyright (c) 2012 Tim van der Molen <tbvdm@xs4all.nl>
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wavpack/wavpack.h>

#include "../siren.h"

#define IP_WAVPACK_BUFSIZE	2048
#define IP_WAVPACK_ERRSTRLEN	80	/* As per the documentation */

struct ip_wavpack_ipdata {
	WavpackContext	*wpc;
	int		 float_samples;
	int32_t		*buf;
	uint32_t	 bufsize;	/* Buffer size, in frames */
	uint32_t	 bufidx;	/* Current sample */
	uint32_t	 buflen;	/* Buffer length, in samples */
};

static void		 ip_wavpack_close(struct track *);
static int		 ip_wavpack_get_metadata(struct track *);
static int		 ip_wavpack_get_position(struct track *,
			    unsigned int *);
static char		*ip_wavpack_get_tag_item_value(WavpackContext *,
			    const char *);
static int		 ip_wavpack_open(struct track *);
static int		 ip_wavpack_read(struct track *, int16_t *, size_t);
static void		 ip_wavpack_seek(struct track *, unsigned int);

static const char	*ip_wavpack_extensions[] = { "wv", NULL };

const struct ip		 ip = {
	"wavpack",
	ip_wavpack_extensions,
	ip_wavpack_close,
	ip_wavpack_get_metadata,
	ip_wavpack_get_position,
	ip_wavpack_open,
	ip_wavpack_read,
	ip_wavpack_seek
};

static void
ip_wavpack_close(struct track *t)
{
	struct ip_wavpack_ipdata *ipd;

	ipd = t->ipdata;
	(void)WavpackCloseFile(ipd->wpc);
	free(ipd->buf);
	free(ipd);
}

/*
 * Perform a rudimentary conversion of a 32-bit IEEE 754 floating-point value
 * between -1 and 1 to an integral value between -32767 and 32767.
 */

#define IP_WAVPACK_FLOAT_SIGN(flt)	((flt) & 0x80000000)
#define IP_WAVPACK_FLOAT_EXP(flt)	(((flt) & 0x7f800000) >> 23)
#define IP_WAVPACK_FLOAT_FRAC(flt)	((flt) & 0x007fffff)

static int16_t
ip_wavpack_float_to_int(int32_t flt)
{
	int32_t exp, frac;

	exp = IP_WAVPACK_FLOAT_EXP(flt);
	frac = IP_WAVPACK_FLOAT_FRAC(flt);

	/* Handle the value 0. */
	if (exp == 0 && frac == 0)
		return 0;

	/* Unbias the exponent. */
	exp -= 127;

	/* Handle the values -1 or less and 1 or greater. */
	if (exp >= 0) {
		if (IP_WAVPACK_FLOAT_SIGN(flt))
			return -INT16_MAX;
		else
			return INT16_MAX;
	}

	/* Add the implicit 24th bit. */
	frac |= 1 << 23;

	/* Perform exponentiation. */
	frac >>= -exp;

	/* There is room only for 16 bits, so discard the 8 LSBs. */
	frac >>= 8;

	if (IP_WAVPACK_FLOAT_SIGN(flt))
		frac = -frac;

	return (int16_t)frac;
}

static int
ip_wavpack_get_metadata(struct track *t)
{
	WavpackContext	*wpc;
	uint32_t	 nframes, rate;
	char		 errstr[IP_WAVPACK_ERRSTRLEN];

	wpc = WavpackOpenFileInput(t->path, errstr, OPEN_TAGS, 0);
	if (wpc == NULL) {
		LOG_ERRX("WavpackOpenFileInput: %s: %s", t->path, errstr);
		msg_errx("%s: Cannot open track: %s", t->path, errstr);
		return -1;
	}

	t->album = ip_wavpack_get_tag_item_value(wpc, "album");
	t->artist = ip_wavpack_get_tag_item_value(wpc, "artist");
	t->date = ip_wavpack_get_tag_item_value(wpc, "year");
	t->genre = ip_wavpack_get_tag_item_value(wpc, "genre");
	t->title = ip_wavpack_get_tag_item_value(wpc, "title");
	t->tracknumber = ip_wavpack_get_tag_item_value(wpc, "track");

	/* The track number may be of the form "x/y". Ignore the "/y" part. */
	if (t->tracknumber != NULL)
		t->tracknumber[strcspn(t->tracknumber, "/")] = '\0';

	nframes = WavpackGetNumSamples(wpc);
	rate = WavpackGetSampleRate(wpc);
	if (nframes == (uint32_t)-1 || rate == 0)
		t->duration = 0;
	else
		t->duration = nframes / rate;

	(void)WavpackCloseFile(wpc);
	return 0;
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
ip_wavpack_get_tag_item_value(WavpackContext *wpc, const char *key)
{
	int	 len;
	char	*value;

	len = WavpackGetTagItem(wpc, key, NULL, 0);
	if (len == 0)
		value = NULL;
	else {
		value = xmalloc(len + 1);
		(void)WavpackGetTagItem(wpc, key, value, len + 1);
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
	else {
		/*
		 * WavPack aligns samples whose bit depth is not a multiple of
		 * 8 on the MSB rather than the LSB. Therefore, determine the
		 * bit depth from the number of bytes per sample.
		 */
		t->format.nbits = 8 * WavpackGetBytesPerSample(wpc);

		/* Only 16-bit samples or less are supported at the moment. */
		if (t->format.nbits > 16) {
			LOG_ERRX("%s: %d bits per sample not supported",
			    t->path, t->format.nbits);
			msg_errx("%s: %d bits per sample not supported",
			    t->path, t->format.nbits);
			(void)WavpackCloseFile(wpc);
			return -1;
		}
	}

	t->format.nchannels = WavpackGetNumChannels(wpc);
	t->format.rate = WavpackGetSampleRate(wpc);

	ipd = xmalloc(sizeof *ipd);
	ipd->wpc = wpc;
	ipd->float_samples = float_samples;
	ipd->bufidx = 0;
	ipd->buflen = 0;
	ipd->bufsize = IP_WAVPACK_BUFSIZE;
	ipd->buf = xcalloc(ipd->bufsize * t->format.nchannels,
	    sizeof *ipd->buf);

	t->ipdata = ipd;
	return 0;
}

static int
ip_wavpack_read(struct track *t, int16_t *samples, size_t maxsamples)
{
	struct ip_wavpack_ipdata	*ipd;
	uint32_t			 ret;
	size_t				 i;

	ipd = t->ipdata;

	for (i = 0; i < maxsamples; i++) {
		if (ipd->bufidx == ipd->buflen) {
			ret = WavpackUnpackSamples(ipd->wpc, ipd->buf,
			    ipd->bufsize);
			if (ret == 0)
				/* EOF reached. */
				break;

			/*
			 * WavpackUnpackSamples() returns the number of frames
			 * read. We need the number of samples read, so
			 * multiply by the number of channels.
			 */
			ipd->buflen = ret * t->format.nchannels;
			ipd->bufidx = 0;
		}

		if (ipd->float_samples)
			samples[i] =
			    ip_wavpack_float_to_int(ipd->buf[ipd->bufidx++]);
		else
			samples[i] = (int16_t)ipd->buf[ipd->bufidx++];
	}

	return (int)i;
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
