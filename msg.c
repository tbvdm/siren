/*
 * Copyright (c) 2011 Tim van der Molen <tbvdm@xs4all.nl>
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

/* Prevent glibc from exposing a non-standard version of strerror_r(). */
#undef _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "siren.h"

static void msg_verr(const char *, va_list) PRINTFLIKE(1, 0);
static void msg_verrx(const char *, va_list) PRINTFLIKE(1, 0);

void
msg_clear(void)
{
	screen_status_clear();
}

void
msg_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	msg_verr(fmt, ap);
	va_end(ap);
}

void
msg_errx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	msg_verrx(fmt, ap);
	va_end(ap);
}

void
msg_info(const char *fmt, ...)
{
	va_list ap;

	if (!prompt_is_active()) {
		va_start(ap, fmt);
		screen_msg_info_vprintf(fmt, ap);
		va_end(ap);
	}
}

void
msg_ip_err(int errnum, const char *errmsg, const char *fmt, ...)
{
	va_list	 ap;
	char	*msg;

	va_start(ap, fmt);
	if (errnum == IP_ERROR_SYSTEM)
		msg_verr(fmt, ap);
	else {
		(void)xvasprintf(&msg, fmt, ap);
		msg_errx("%s: %s", msg, errmsg);
		free(msg);
	}
	va_end(ap);
}

void
msg_op_err(const struct op *op, int errnum, const char *fmt, ...)
{
	va_list	ap;
	char	errstr[STRERROR_BUFSIZE], *msg;

	va_start(ap, fmt);
	(void)xvasprintf(&msg, fmt, ap);
	va_end(ap);

	if (errnum < 0)
		msg_errx("%s: %s", msg, op->error(errnum));
	else {
		(void)strerror_r(errnum, errstr, sizeof errstr);
		msg_errx("%s: %s", msg, errstr);
	}

	free(msg);
}

static void
msg_verr(const char *fmt, va_list ap)
{
	int	oerrno;
	char	errstr[STRERROR_BUFSIZE], *msg;

	oerrno = errno;
	if (!prompt_is_active()) {
		(void)strerror_r(oerrno, errstr, sizeof errstr);
		(void)xvasprintf(&msg, fmt, ap);
		screen_msg_error_printf("%s: %s", msg, errstr);
		free(msg);
	}
	errno = oerrno;
}

static void
msg_verrx(const char *fmt, va_list ap)
{
	if (!prompt_is_active())
		screen_msg_error_vprintf(fmt, ap);
}
