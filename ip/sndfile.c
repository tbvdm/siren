/*
 * Copyright (c) 2011, 2012 Tim van der Molen <tbvdm@xs4all.nl>
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

/* Silence gcc. */
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 0

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <sndfile.h>

#include "../siren.h"

#define IP_SNDFILE_BUFSIZE 4096

struct ip_sndfile_ipdata {
	SNDFILE		*sffp;
	SF_INFO		 sfinfo;

	short int	*buf;
	sf_count_t	 bufidx;
	sf_count_t	 buflen;

	/* Current position in track, measured in samples. */
	sf_count_t	 position;
};

static void		 ip_sndfile_close(struct track *);
static int		 ip_sndfile_get_metadata(struct track *, char **);
static int		 ip_sndfile_get_position(struct track *,
			    unsigned int *, char **);
static int		 ip_sndfile_open(struct track *, char **);
static int		 ip_sndfile_read(struct track *, int16_t *, size_t,
			    char **);
static int		 ip_sndfile_seek(struct track *, unsigned int,
			    char **);

/*
 * Based on <http://www.mega-nerd.com/libsndfile/> and src/command.c in the
 * libsndfile distribution.
 */
static const char	*ip_sndfile_extensions[] = {
	"aif", "aifc", "aiff",	/* AIFF; AIFF-C (compressed) */
	"au", "snd",		/* NeXT/Sun audio */
	"avr",			/* Audio Visual Research */
	"caf",			/* Apple Core Audio Format */
	"htk",			/* Hidden Markov Model Toolkit */
	"iff", "svx",		/* Commodore Amiga IFF/8SVX */
	"mat",			/* MATLAB or GNU Octave */
	"mpc",			/* Akai Music Production Center */
	"nist", "sph",		/* NIST/Sphere WAVE */
	"paf",			/* Ensoniq PARIS audio file */
	"pvf",			/* Portable Voice Format */
	"rf64",			/* EBU MBWF/RF64 */
	"sd2",			/* Sound Designer II */
	"sds",			/* MIDI Sample Dump Standard */
	"sf",			/* IRCAM SF */
	"voc",			/* Creative Sound Blaster voice */
	"w64",			/* Sony Sound Forge Wave64 */
	"wav", "wave",		/* Microsoft WAVE */
	"wve",			/* Psion Series 3 WVE */
	"xi",			/* FastTracker 2 XI */
	NULL
};

const struct ip		 ip = {
	"sndfile",
	ip_sndfile_extensions,
	ip_sndfile_close,
	ip_sndfile_get_metadata,
	ip_sndfile_get_position,
	ip_sndfile_open,
	ip_sndfile_read,
	ip_sndfile_seek
};

static void
ip_sndfile_close(struct track *t)
{
	struct ip_sndfile_ipdata *ipd;
	int ret;

	ipd = t->ipdata;

	if ((ret = sf_close(ipd->sffp)) != 0)
		LOG_ERRX("sf_close: %s: %s", t->path, sf_error_number(ret));
	free(ipd->buf);
	free(ipd);
}

static int
ip_sndfile_get_metadata(struct track *t, char **error)
{
	SNDFILE		*sffp;
	SF_INFO		 sfinfo;
	int		 fd, ret;
	const char	*value;

	if ((fd = open(t->path, O_RDONLY)) == -1) {
		LOG_ERR("open: %s", t->path);
		return IP_ERROR_SYSTEM;
	}

	sfinfo.format = 0;
	if ((sffp = sf_open_fd(fd, SFM_READ, &sfinfo, SF_TRUE)) == NULL) {
		*error = xstrdup(sf_strerror(sffp));
		LOG_ERRX("sf_open_fd: %s: %s", t->path, *error);
		(void)close(fd);
		return IP_ERROR_PLUGIN;
	}

	if ((value = sf_get_string(sffp, SF_STR_ALBUM)) != NULL)
		t->album = xstrdup(value);
	if ((value = sf_get_string(sffp, SF_STR_ARTIST)) != NULL)
		t->artist = xstrdup(value);
	if ((value = sf_get_string(sffp, SF_STR_DATE)) != NULL)
		t->date = xstrdup(value);
#ifdef HAVE_SF_STR_GENRE
	if ((value = sf_get_string(sffp, SF_STR_GENRE)) != NULL)
		t->genre = xstrdup(value);
#endif
	if ((value = sf_get_string(sffp, SF_STR_TITLE)) != NULL)
		t->title = xstrdup(value);
#ifdef HAVE_SF_STR_TRACKNUMBER
	if ((value = sf_get_string(sffp, SF_STR_TRACKNUMBER)) != NULL)
		t->tracknumber = xstrdup(value);
#endif

	if (sfinfo.frames < 0 || sfinfo.samplerate <= 0)
		t->duration = 0;
	else
		t->duration = sfinfo.frames / sfinfo.samplerate;

	if ((ret = sf_close(sffp)))
		LOG_ERRX("sf_close: %s: %s", t->path, sf_error_number(ret));

	return 0;
}

/* ARGSUSED2 */
static int
ip_sndfile_get_position(struct track *t, unsigned int *pos,
    UNUSED char **error)
{
	struct ip_sndfile_ipdata *ipd;

	ipd = t->ipdata;

	if (ipd->sfinfo.channels <= 0 || ipd->sfinfo.samplerate <= 0)
		*pos = 0;
	else
		*pos = (unsigned int)(ipd->position / ipd->sfinfo.channels /
		    ipd->sfinfo.samplerate);

	return 0;
}

static int
ip_sndfile_open(struct track *t, char **error)
{
	struct ip_sndfile_ipdata *ipd;
	int fd;

	if ((fd = open(t->path, O_RDONLY)) == -1) {
		LOG_ERR("open: %s", t->path);
		return IP_ERROR_SYSTEM;
	}

	ipd = xmalloc(sizeof *ipd);

	ipd->sfinfo.format = 0;
	if ((ipd->sffp = sf_open_fd(fd, SFM_READ, &ipd->sfinfo, SF_TRUE)) ==
	    NULL) {
		*error = xstrdup(sf_strerror(ipd->sffp));
		LOG_ERRX("sf_open_fd: %s: %s", t->path, *error);
		free(ipd);
		(void)close(fd);
		return IP_ERROR_PLUGIN;
	}

	t->format.nbits = 16;
	t->format.nchannels = ipd->sfinfo.channels;
	t->format.rate = ipd->sfinfo.samplerate;

	ipd->buf = xcalloc(IP_SNDFILE_BUFSIZE, sizeof *ipd->buf);
	ipd->bufidx = 0;
	ipd->buflen = 0;
	ipd->position = 0;

	t->ipdata = ipd;
	return 0;
}

static int
ip_sndfile_read(struct track *t, int16_t *samples, size_t maxsamples,
    char **error)
{
	struct ip_sndfile_ipdata *ipd;
	size_t nsamples;

	ipd = t->ipdata;

	for (nsamples = 0; nsamples < maxsamples; nsamples++) {
		if (ipd->bufidx == ipd->buflen) {
			/* Fill the buffer. */
			ipd->bufidx = 0;
			ipd->buflen = sf_read_short(ipd->sffp, ipd->buf,
			    IP_SNDFILE_BUFSIZE);

			/* Check for error. */
			if (sf_error(ipd->sffp) != SF_ERR_NO_ERROR) {
				*error = xstrdup(sf_strerror(ipd->sffp));
				LOG_ERRX("sf_read_short: %s: %s", t->path,
				    *error);
				return IP_ERROR_PLUGIN;
			}

			/* Check for EOF. */
			if (ipd->buflen == 0)
				break;
		}

		samples[nsamples] = (int16_t)ipd->buf[ipd->bufidx++];
	}

	ipd->position += nsamples;
	return (int)nsamples;
}

static int
ip_sndfile_seek(struct track *t, unsigned int pos, char **error)
{
	struct ip_sndfile_ipdata *ipd;
	sf_count_t frame, seekframe;

	ipd = t->ipdata;

	seekframe = (sf_count_t)pos * ipd->sfinfo.samplerate;
	if ((frame = sf_seek(ipd->sffp, seekframe, SEEK_SET)) == -1) {
		*error = xstrdup(sf_strerror(ipd->sffp));
		LOG_ERRX("sf_seek: %s: %s", t->path, *error);
		return IP_ERROR_PLUGIN;
	}

	ipd->position = frame * ipd->sfinfo.channels;

	/* Discard buffered samples from the old position. */
	ipd->bufidx = 0;
	ipd->buflen = 0;

	return 0;
}
