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

#include <sys/types.h>
#include <sys/utsname.h>

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "siren.h"

#ifdef HAVE_ERR
#include <err.h>
#endif

static void		 log_verr(const char *, const char *, va_list)
			    PRINTFLIKE(2, 0);
static void		 log_vprintf(const char *, const char *, va_list)
			    PRINTFLIKE(2, 0);

static pthread_mutex_t	 log_fp_mtx = PTHREAD_MUTEX_INITIALIZER;
static FILE		*log_fp;
static int		 log_enabled;

void
log_end(void)
{
	if (log_enabled)
		(void)fclose(log_fp);
}

void
log_err(const char *func, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_verr(func, fmt, ap);
	va_end(ap);
}

void
log_errx(const char *func, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_vprintf(func, fmt, ap);
	va_end(ap);
}

void
log_fatal(const char *func, const char *fmt, ...)
{
	va_list ap, ap2;

	va_start(ap, fmt);
	va_copy(ap2, ap);
	log_verr(func, fmt, ap2);
	va_end(ap2);
	verr(1, fmt, ap);
}

void
log_fatalx(const char *func, const char *fmt, ...)
{
	va_list ap, ap2;

	va_start(ap, fmt);
	va_copy(ap2, ap);
	log_vprintf(func, fmt, ap2);
	va_end(ap2);
	verrx(1, fmt, ap);
}

void
log_info(const char *func, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_vprintf(func, fmt, ap);
	va_end(ap);
}

void
log_init(int enable)
{
	struct utsname	 un;
	char		*file;

	if (enable) {
		(void)xasprintf(&file, "siren-%ld.log", (long int)getpid());
		if ((log_fp = fopen(file, "w")) == NULL)
			err(1, "fopen: %s", file);
		free(file);

		setbuf(log_fp, NULL);
		log_enabled = 1;

		if (uname(&un) != -1)
			log_info(NULL, "siren %s (%s %s %s)", VERSION,
			    un.sysname, un.release, un.machine);
	}
}

PRINTFLIKE(2, 0) static void
log_printf(const char *func, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_vprintf(func, fmt, ap);
	va_end(ap);
}

static void
log_verr(const char *func, const char *fmt, va_list ap)
{
	int	oerrno;
	char	errstr[STRERROR_BUFSIZE], *msg;

	oerrno = errno;
	(void)strerror_r(errno, errstr, sizeof errstr);
	(void)xvasprintf(&msg, fmt, ap);
	log_printf(func, "%s: %s", msg, errstr);
	free(msg);
	errno = oerrno;
}

static void
log_vprintf(const char *func, const char *fmt, va_list ap)
{
	if (log_enabled) {
		(void)pthread_mutex_lock(&log_fp_mtx);
		if (func != NULL)
			(void)fprintf(log_fp, "%s: ", func);
		if (fmt != NULL)
			(void)vfprintf(log_fp, fmt, ap);
		(void)putc('\n', log_fp);
		(void)pthread_mutex_unlock(&log_fp_mtx);
	}
}
