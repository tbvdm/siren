/*
 * Copyright (c) 2014 Tim van der Molen <tim@kariliq.nl>
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

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mpg123.h>

#include "../siren.h"

struct ip_mpg123_ipdata {
	mpg123_handle	*hdl;
	int		 fd;
};

static void	 ip_mpg123_close(struct track *);
static void	 ip_mpg123_close_fd_handle(int fd, mpg123_handle *);
static int	 ip_mpg123_get_position(struct track *, unsigned int *);
static void	 ip_mpg123_get_metadata(struct track *);
static int	 ip_mpg123_init(void);
static int	 ip_mpg123_open(struct track *);
static int	 ip_mpg123_open_fd_handle(const char *, int *fd,
		    mpg123_handle **);
static int	 ip_mpg123_read(struct track *, struct sample_buffer *);
static void	 ip_mpg123_seek(struct track *, unsigned int);

static const char *ip_mpg123_extensions[] = { "mp1", "mp2", "mp3", NULL };

static const char *ip_mpg123_genres[] = {
	/*
	 * ID3v1 genres (based on https://id3.org/id3v2.4.0-frames)
	 */

	"Blues",
	"Classic Rock",
	"Country",
	"Dance",
	"Disco",
	"Funk",
	"Grunge",
	"Hip-Hop",
	"Jazz",
	"Metal",
	"New Age",
	"Oldies",
	"Other",
	"Pop",
	"R&B",
	"Rap",
	"Reggae",
	"Rock",
	"Techno",
	"Industrial",
	"Alternative",
	"Ska",
	"Death Metal",
	"Pranks",
	"Soundtrack",
	"Euro-Techno",
	"Ambient",
	"Trip-Hop",
	"Vocal",
	"Jazz+Funk",
	"Fusion",
	"Trance",
	"Classical",
	"Instrumental",
	"Acid",
	"House",
	"Game",
	"Sound Clip",
	"Gospel",
	"Noise",
	"AlternRock",
	"Bass",
	"Soul",
	"Punk",
	"Space",
	"Meditative",
	"Instrumental Pop",
	"Instrumental Rock",
	"Ethnic",
	"Gothic",
	"Darkwave",
	"Techno-Industrial",
	"Electronic",
	"Pop-Folk",
	"Eurodance",
	"Dream",
	"Southern Rock",
	"Comedy",
	"Cult",
	"Gangsta",
	"Top 40",
	"Christian Rap",
	"Pop/Funk",
	"Jungle",
	"Native American",
	"Cabaret",
	"New Wave",
	"Psychedelic",
	"Rave",
	"Showtunes",
	"Trailer",
	"Lo-Fi",
	"Tribal",
	"Acid Punk",
	"Acid Jazz",
	"Polka",
	"Retro",
	"Musical",
	"Rock & Roll",
	"Hard Rock",

	/*
	 * Winamp extensions (based on https://en.wikipedia.org/wiki/ID3)
	 */

	"Folk",
	"Folk Rock",
	"National Folk",
	"Swing",
	"Fast Fusion",
	"Bebop",
	"Latin",
	"Revival",
	"Celtic",
	"Bluegrass",
	"Avantgarde",
	"Gothic Rock",
	"Progressive Rock",
	"Psychedelic Rock",
	"Symphonic Rock",
	"Slow Rock",
	"Big Band",
	"Chorus",
	"Easy Listening",
	"Acoustic",
	"Humour",
	"Speech",
	"Chanson",
	"Opera",
	"Chamber Music",
	"Sonata",
	"Symphony",
	"Booty Bass",
	"Primus",
	"Porn Groove",
	"Satire",
	"Slow Jam",
	"Club",
	"Tango",
	"Samba",
	"Folklore",
	"Ballad",
	"Power Ballad",
	"Rhythmic Soul",
	"Freestyle",
	"Duet",
	"Punk Rock",
	"Drum Solo",
	"A Cappella",
	"Euro-House",
	"Dance Hall",
	"Goa",
	"Drum & Bass",
	"Club-House",
	"Hardcore",
	"Terror",
	"Indie",
	"Britpop",
	"Afro-punk",
	"Polsk Punk",
	"Beat",
	"Christian Gangsta Rap",
	"Heavy Metal",
	"Black Metal",
	"Crossover",
	"Contemporary Christian",
	"Christian Rock",
	"Merengue",
	"Salsa",
	"Thrash Metal",
	"Anime",
	"Jpop",
	"Synthpop",
	"Abstract",
	"Art Rock",
	"Baroque",
	"Bhangra",
	"Big Beat",
	"Breakbeat",
	"Chillout",
	"Downtempo",
	"Dub",
	"EBM",
	"Eclectic",
	"Electro",
	"Electroclash",
	"Emo",
	"Experimental",
	"Garage",
	"Global",
	"IDM",
	"Illbient",
	"Industro-Goth",
	"Jam Band",
	"Krautrock",
	"Leftfield",
	"Lounge",
	"Math Rock",
	"New Romantic",
	"Nu-Breakz",
	"Post-Punk",
	"Post-Rock",
	"Psytrance",
	"Shoegaze",
	"Space Rock",
	"Trop Rock",
	"World Music",
	"Neoclassical",
	"Audiobook",
	"Audio Theatre",
	"Neue Deutsche Welle",
	"Podcast",
	"Indie Rock",
	"G-Funk",
	"Dubstep",
	"Garage Rock",
	"Psybient"
};

const struct ip	 ip = {
	"mpg123",
	IP_PRIORITY_MPG123,
	ip_mpg123_extensions,
	ip_mpg123_close,
	ip_mpg123_get_metadata,
	ip_mpg123_get_position,
	ip_mpg123_init,
	ip_mpg123_open,
	ip_mpg123_read,
	ip_mpg123_seek
};

static void
ip_mpg123_close(struct track *t)
{
	struct ip_mpg123_ipdata *ipd;

	ipd = t->ipdata;

	ip_mpg123_close_fd_handle(ipd->fd, ipd->hdl);
	free(ipd);
}

static void
ip_mpg123_close_fd_handle(int fd, mpg123_handle *hdl)
{
	mpg123_close(hdl);
	mpg123_delete(hdl);
	close(fd);
}

static char *
ip_mpg123_get_genre(mpg123_string *s)
{
	int		 idx;
	char		*t;
	const char	*errstr;

	/*
	 * If the TCON (genre) frame is of the form "n" or "(n)" where "n" is
	 * a numerical string, then assume "n" is an ID3v1 genre index.
	 */

	if (s->p[0] != '(')
		t = s->p;
	else {
		t = s->p + 1;
		t[strcspn(t, ")")] = '\0';
	}

	idx = strtonum(t, 0, nitems(ip_mpg123_genres) - 1, &errstr);
	if (errstr == NULL)
		return xstrdup(ip_mpg123_genres[idx]);
	else
		return xstrdup(s->p);
}

static void
ip_mpg123_get_metadata(struct track *t)
{
	mpg123_handle	*hdl;
	mpg123_id3v1	*v1;
	mpg123_id3v2	*v2;
	off_t		 length;
	size_t		 i;
	long		 rate;
	int		 encoding, fd, nchannels;

	if (ip_mpg123_open_fd_handle(t->path, &fd, &hdl) == -1)
		return;

	if (mpg123_getformat(hdl, &rate, &nchannels, &encoding) != MPG123_OK) {
		LOG_ERRX("mpg123_getformat: %s: %s", t->path,
		    mpg123_strerror(hdl));
		msg_errx("%s: Cannot get format: %s", t->path,
		    mpg123_strerror(hdl));
		goto out;
	}

	if (mpg123_scan(hdl) != MPG123_OK) {
		LOG_ERRX("msg123_scan: %s: %s", t->path, mpg123_strerror(hdl));
		msg_errx("%s: Cannot scan track: %s", t->path,
		    mpg123_strerror(hdl));
		goto out;
	}

	length = mpg123_length(hdl);
	if (length > 0 && rate > 0)
		t->duration = length / rate;

	if (mpg123_id3(hdl, &v1, &v2) != MPG123_OK) {
		LOG_ERRX("mpg123_id3: %s: %s", t->path, mpg123_strerror(hdl));
		msg_errx("%s: Cannot get metadata: %s", t->path,
		    mpg123_strerror(hdl));
		goto out;
	}

	if (v2 != NULL) {
		for (i = 0; i < v2->texts; i++)
			if (!strncmp(v2->text[i].id, "TALB", 4))
				t->album = xstrdup(v2->text[i].text.p);
			else if (!strncmp(v2->text[i].id, "TPE2", 4))
				t->albumartist = xstrdup(v2->text[i].text.p);
			else if (!strncmp(v2->text[i].id, "TPE1", 4))
				t->artist = xstrdup(v2->text[i].text.p);
			else if (!strncmp(v2->text[i].id, "COMM", 4))
				t->comment = xstrdup(v2->text[i].text.p);
			else if (!strncmp(v2->text[i].id, "TDRC", 4) ||
			    !strncmp(v2->text[i].id, "TYER", 4))
				t->date = xstrdup(v2->text[i].text.p);
			else if (!strncmp(v2->text[i].id, "TPOS", 4))
				track_split_tag(v2->text[i].text.p,
				    &t->discnumber, &t->disctotal);
			else if (!strncmp(v2->text[i].id, "TCON", 4))
				t->genre =
				    ip_mpg123_get_genre(&v2->text[i].text);
			else if (!strncmp(v2->text[i].id, "TIT2", 4))
				t->title = xstrdup(v2->text[i].text.p);
			else if (!strncmp(v2->text[i].id, "TRCK", 4))
				track_split_tag(v2->text[i].text.p,
				    &t->tracknumber, &t->tracktotal);
	} else if (v1 != NULL) {
		t->album = xstrndup(v1->album, sizeof v1->album);
		t->artist = xstrndup(v1->artist, sizeof v1->artist);
		t->date = xstrndup(v1->year, sizeof v1->year);
		t->title = xstrndup(v1->title, sizeof v1->title);

		if (v1->genre < nitems(ip_mpg123_genres))
			t->genre = xstrdup(ip_mpg123_genres[v1->genre]);

		/*
		 * ID3v1.1 allows the track number to be stored in the comment
		 * field: if byte 28 is 0, then byte 29 is the track number.
		 */
		if (v1->comment[28] == '\0')
			xasprintf(&t->tracknumber, "%d", v1->comment[29]);
	}

out:
	ip_mpg123_close_fd_handle(fd, hdl);
}

static int
ip_mpg123_get_position(struct track *t, unsigned int *pos)
{
	struct ip_mpg123_ipdata	*ipd;
	off_t			 samplepos;

	ipd = t->ipdata;

	samplepos = mpg123_tell(ipd->hdl);
	*pos = samplepos / t->format.rate;
	return 0;
}

static int
ip_mpg123_init(void)
{
	int ret;

	ret = mpg123_init();
	if (ret == MPG123_OK)
		return 0;
	else {
		LOG_ERRX("mpg123_init: %s", mpg123_plain_strerror(ret));
		msg_errx("Cannot initialise libmpg123: %s",
		    mpg123_plain_strerror(ret));
		return -1;
	}

}

static int
ip_mpg123_open(struct track *t)
{
	struct ip_mpg123_ipdata	*ipd;
	mpg123_handle		*hdl;
	long			 rate;
	int			 encoding, fd, nchannels;

	if (ip_mpg123_open_fd_handle(t->path, &fd, &hdl) == -1)
		return -1;

	if (mpg123_getformat(hdl, &rate, &nchannels, &encoding) != MPG123_OK) {
		LOG_ERRX("mpg123_getformat: %s: %s", t->path,
		    mpg123_strerror(hdl));
		msg_errx("%s: Cannot get format: %s", t->path,
		    mpg123_strerror(hdl));
		goto error;
	}

	switch (encoding) {
	case MPG123_ENC_SIGNED_8:
		t->format.nbits = 8;
		break;
	case MPG123_ENC_SIGNED_16:
		t->format.nbits = 16;
		break;
	case MPG123_ENC_SIGNED_24:
		t->format.nbits = 24;
		break;
	case MPG123_ENC_SIGNED_32:
		t->format.nbits = 32;
		break;
	default:
		LOG_ERRX("%s: %#x: unsupported encoding", t->path, encoding);
		msg_errx("%s: Unsupported encoding", t->path);
		goto error;
	}

	/* Ensure the output format will not change. */
	mpg123_format_none(hdl);
	mpg123_format(hdl, rate, nchannels, encoding);

	t->format.nchannels = nchannels;
	t->format.rate = rate;

	ipd = xmalloc(sizeof *ipd);
	ipd->hdl = hdl;
	ipd->fd = fd;
	t->ipdata = ipd;

	return 0;

error:
	ip_mpg123_close_fd_handle(fd, hdl);
	return -1;
}

static int
ip_mpg123_open_fd_handle(const char *path, int *fd, mpg123_handle **hdl)
{
	int err;

	*fd = open(path, O_RDONLY);
	if (*fd == -1) {
		LOG_ERR("open: %s", path);
		msg_err("%s: Cannot open track", path);
		return -1;
	}

	*hdl = mpg123_new(NULL, &err);
	if (*hdl == NULL) {
		LOG_ERRX("mpg123_new: %s", mpg123_plain_strerror(err));
		msg_errx("Cannot create handle: %s",
		    mpg123_plain_strerror(err));
		close(*fd);
		return -1;
	}

	/* Thank you for not writing to stderr. */
	mpg123_param(*hdl, MPG123_ADD_FLAGS, MPG123_QUIET, 0.0);

	if (mpg123_open_fd(*hdl, *fd) != MPG123_OK) {
		LOG_ERRX("mpg123_open_fd: %s: %s", path,
		    mpg123_strerror(*hdl));
		msg_errx("%s: Cannot open track: %s", path,
		    mpg123_strerror(*hdl));
		mpg123_delete(*hdl);
		close(*fd);
		return -1;
	}

	return 0;
}

static int
ip_mpg123_read(struct track *t, struct sample_buffer *sb)
{
	struct ip_mpg123_ipdata	*ipd;
	int			 ret;

	ipd = t->ipdata;

	ret = mpg123_read(ipd->hdl, (unsigned char *)sb->data, sb->size_b,
	    &sb->len_b);
	if (ret != MPG123_OK && ret != MPG123_DONE) {
		LOG_ERRX("%s: mpg123_read: %s", t->path,
		    mpg123_strerror(ipd->hdl));
		msg_errx("Cannot read from track: %s",
		    mpg123_strerror(ipd->hdl));
		return -1;
	}

	sb->len_s = sb->len_b / sb->nbytes;
	return sb->len_s != 0;
}

static void
ip_mpg123_seek(struct track *t, unsigned int pos)
{
	struct ip_mpg123_ipdata	*ipd;
	off_t			 samplepos;

	ipd = t->ipdata;

	samplepos = pos * t->format.rate;
	if (mpg123_seek(ipd->hdl, samplepos, SEEK_SET) < 0) {
		LOG_ERRX("mpg123_seek: %s: %s", t->path,
		    mpg123_strerror(ipd->hdl));
		msg_errx("Cannot seek: %s", mpg123_strerror(ipd->hdl));
	}
}
