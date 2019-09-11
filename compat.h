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

#ifndef COMPAT_H
#define COMPAT_H

#include "attribute.h"

#ifdef __OpenBSD__
#include <sys/queue.h>
#include <sys/tree.h>
#else
#include "compat/queue.h"
#include "compat/tree.h"
#endif

#ifndef HAVE_ASPRINTF
int		 asprintf(char **, const char *, ...) NONNULL()
		    PRINTFLIKE(2, 3);
int		 vasprintf(char **, const char *, va_list) NONNULL()
		    PRINTFLIKE(2, 0);
#endif

#ifdef HAVE_ERR
#include <err.h>
#else
void		 err(int, const char *, ...) NORETURN PRINTFLIKE(2, 3);
void		 errx(int, const char *, ...) NORETURN PRINTFLIKE(2, 3);
void		 verr(int, const char *, va_list) NORETURN PRINTFLIKE(2, 0);
void		 verrx(int, const char *, va_list) NORETURN PRINTFLIKE(2, 0);
void		 vwarn(const char *, va_list) PRINTFLIKE(1, 0);
void		 vwarnx(const char *, va_list) PRINTFLIKE(1, 0);
void		 warn(const char *, ...) PRINTFLIKE(1, 2);
void		 warnx(const char *, ...) PRINTFLIKE(1, 2);
#endif

#ifndef HAVE_OPTRESET
#define getopt		xgetopt
#define optarg		xoptarg
#define opterr		xopterr
#define optind		xoptind
#define optopt		xoptopt
#define optreset	xoptreset

extern int	 xopterr, xoptind, xoptopt, xoptreset;
extern char	*xoptarg;

int		 xgetopt(int, char * const *, const char *);
#endif

#ifndef HAVE_REALLOCARRAY
void		*reallocarray(void *, size_t, size_t);
#endif

#ifndef HAVE_STRCASESTR
char		*strcasestr(const char *, const char *);
#endif

#ifndef HAVE_STRLCAT
size_t		 strlcat(char *, const char *, size_t);
#endif

#ifndef HAVE_STRLCPY
size_t		 strlcpy(char *, const char *, size_t);
#endif

#ifndef HAVE_STRSEP
char		*strsep(char **, const char *);
#endif

#ifndef HAVE_STRTONUM
long long int	 strtonum(const char *, long long, long long, const char **);
#endif

#endif
