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

/*
 * Prevent <vorbis/vorbisfile.h> from defining unused variables. This, in turn,
 * prevents gcc from warning about them.
 */
#define OV_EXCLUDE_STATIC_CALLBACKS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <vorbis/vorbisfile.h>

#include "../siren.h"

static void		 ip_vorbis_close(struct track *);
static const char	*ip_vorbis_error(int);
static void		 ip_vorbis_get_metadata(struct track *);
static int		 ip_vorbis_get_position(struct track *,
			    unsigned int *);
static int		 ip_vorbis_open(struct track *);
static int		 ip_vorbis_read(struct track *, int16_t *, size_t);
static void		 ip_vorbis_seek(struct track *, unsigned int);

static const char	*ip_vorbis_extensions[] = { "oga", "ogg", NULL };

const struct ip		 ip = {
	"vorbis",
	ip_vorbis_extensions,
	ip_vorbis_close,
	ip_vorbis_get_metadata,
	ip_vorbis_get_position,
	ip_vorbis_open,
	ip_vorbis_read,
	ip_vorbis_seek
};

static void
ip_vorbis_close(struct track *t)
{
	OggVorbis_File *ovf;

	ovf = t->ipdata;
	ov_clear(ovf);
	free(ovf);
}

static const char *
ip_vorbis_error(int errnum)
{
	/*
	 * See <http://www.xiph.org/vorbis/doc/vorbis-errors.txt> and
	 * <http://www.xiph.org/vorbis/doc/libvorbis/return.html>.
	 */
	switch (errnum) {
	case OV_EOF:
		return "End of file after seeking";
	case OV_FALSE:
		return "False";
	case OV_HOLE:
		return "Data interruption";
	case OV_EBADHEADER:
		return "Invalid bitstream header";
	case OV_EBADLINK:
		return "Invalid stream section or corrupted link";
	case OV_EBADPACKET:
		return "Invalid packet";
	case OV_EFAULT:
		return "Internal logic fault";
	case OV_EIMPL:
		return "Feature not implemented";
	case OV_EINVAL:
		return "Invalid argument value";
	case OV_ENOSEEK:
		return "Bitstream not seekable";
	case OV_ENOTAUDIO:
		return "Not audio data";
	case OV_ENOTVORBIS:
		return "Not Vorbis data";
	case OV_EREAD:
		return "Read error";
	case OV_EVERSION:
		return "Vorbis version mismatch";
	default:
		return "Unknown error";
	}
}

static void
ip_vorbis_get_metadata(struct track *t)
{
	OggVorbis_File	  ovf;
	vorbis_comment	 *vc;
	FILE		 *fp;
	double		  duration;
	int		  ret;
	char		**c;

	if ((fp = fopen(t->path, "r")) == NULL) {
		LOG_ERR("fopen: %s", t->path);
		msg_err("%s: Cannot open track", t->path);
		return;
	}

	if ((ret = ov_open(fp, &ovf, NULL, 0)) != 0) {
		LOG_ERRX("ov_open: %s: %s", t->path, ip_vorbis_error(ret));
		msg_errx("%s: Cannot open track: %s", t->path,
		    ip_vorbis_error(ret));
		fclose(fp);
		return;
	}

	if ((vc = ov_comment(&ovf, -1)) == NULL) {
		LOG_ERRX("%s: ov_comment() failed", t->path);
		msg_errx("%s: Cannot get Vorbis comments", t->path);
		ov_clear(&ovf);
		return;
	}

	for (c = vc->user_comments; *c != NULL; c++)
		track_set_vorbis_comment(t, *c);

	if ((duration = ov_time_total(&ovf, -1)) != OV_EINVAL)
		t->duration = duration;
	else {
		LOG_ERRX("%s: ov_time_total() failed", t->path);
		msg_errx("%s: Cannot get track duration", t->path);
	}

	ov_clear(&ovf);
}

static int
ip_vorbis_get_position(struct track *t, unsigned int *pos)
{
	OggVorbis_File	*ovf;
	double		 ret;

	ovf = t->ipdata;
	if ((ret = ov_time_tell(ovf)) == OV_EINVAL) {
		LOG_ERRX("ov_time_tell: %s: %s", t->path,
		    ip_vorbis_error(ret));
		msg_errx("Cannot get track position: %s",
		    ip_vorbis_error(ret));
		*pos = 0;
		return -1;
	}

	*pos = ret;
	return 0;
}

static int
ip_vorbis_open(struct track *t)
{
	OggVorbis_File	*ovf;
	vorbis_info	*info;
	FILE		*fp;
	int		 ret;

	if ((fp = fopen(t->path, "r")) == NULL) {
		LOG_ERR("fopen: %s", t->path);
		msg_err("%s: Cannot open track", t->path);
		return -1;
	}

	ovf = xmalloc(sizeof *ovf);

	if ((ret = ov_open(fp, ovf, NULL, 0)) != 0) {
		LOG_ERRX("ov_open: %s: %s", t->path, ip_vorbis_error(ret));
		msg_errx("%s: Cannot open track: %s", t->path,
		    ip_vorbis_error(ret));
		fclose(fp);
		free(ovf);
		return -1;
	}

	if ((info = ov_info(ovf, -1)) == NULL) {
		LOG_ERRX("%s: ov_info() failed", t->path);
		msg_errx("%s: Cannot get bitstream information", t->path);
		ov_clear(ovf);
		free(ovf);
		return -1;
	}

	t->format.nbits = 16;
	t->format.nchannels = info->channels;
	t->format.rate = info->rate;

	t->ipdata = ovf;
	return 0;
}

static int
ip_vorbis_read(struct track *t, int16_t *samples, size_t maxsamples)
{
	OggVorbis_File	*ovf;
	int		 endian, len, ret, size, stream;

	ovf = t->ipdata;
	endian = t->format.byte_order == BYTE_ORDER_BIG;
	len = 0;
	size = maxsamples * 2;

	do
		ret = ov_read(ovf, (char *)samples + len, size - len, endian,
		    2, 1, &stream);
	while (ret > 0 && (len += ret) < size);

	if (ret < 0) {
		LOG_ERRX("ov_read: %s: %s", t->path, ip_vorbis_error(ret));
		msg_errx("Cannot read from track: %s", ip_vorbis_error(ret));
		return -1;
	}

	return len / 2;
}

static void
ip_vorbis_seek(struct track *t, unsigned int sec)
{
	OggVorbis_File	*ovf;
	int		 ret;

	ovf = t->ipdata;
	if ((ret = ov_time_seek_lap(ovf, sec)) != 0) {
		LOG_ERRX("ov_time_seek_lap: %s: %s", t->path,
		    ip_vorbis_error(ret));
		msg_errx("Cannot seek: %s", ip_vorbis_error(ret));
	}
}
