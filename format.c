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

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "siren.h"

enum format_align {
	FORMAT_ALIGN_LEFT,
	FORMAT_ALIGN_RIGHT
};

static const char *
format_get_field(char spec, const struct format_field *fields, size_t nfields)
{
	while (nfields-- > 0)
		if (fields[nfields].spec == spec)
			return fields[nfields].value;

	return NULL;
}

int
format_snprintf(char *buf, size_t bufsize, const char *fmt,
    const struct format_field *fields, size_t nfields)
{
	size_t			 fieldlen, i, j, len, maxfieldlen, maxlen,
				 nvarlenfields, padlen, varlen;
	enum format_align	 align;
	const char		*field;

	maxlen = bufsize - 1;

	len = nvarlenfields = 0;
	for (i = 0; fmt[i] != '\0'; i++) {
		if (fmt[i] != '%' || fmt[++i] == '%') {
			len++;
			continue;
		}

		if (fmt[i] == '-')
			i++;

		if (!isdigit((int)fmt[i])) {
			field = format_get_field(fmt[i], fields, nfields);
			if (field == NULL) {
				buf[0] = '\0';
				return -1;
			}
			fieldlen = strlen(field);
		} else {
			fieldlen = 0;
			do
				fieldlen = 10 * fieldlen + (fmt[i] - '0');
			while (isdigit((int)fmt[++i]));

			if (fieldlen == 0) {
				nvarlenfields++;
				continue;
			}
		}

		len += fieldlen;
	}

	if (len >= maxlen)
		varlen = 0;
	else if (nvarlenfields)
		varlen = maxlen - len;
	else {
		maxlen = len;
		varlen = 0;
	}

	len = 0;
	for (i = 0; fmt[i] != '\0' && len < maxlen; i++) {
		if (fmt[i] != '%' || fmt[++i] == '%') {
			buf[len++] = fmt[i];
			continue;
		}

		if (fmt[i] != '-')
			align = FORMAT_ALIGN_RIGHT;
		else {
			align = FORMAT_ALIGN_LEFT;
			i++;
		}

		maxfieldlen = 0;
		while (isdigit((int)fmt[i])) {
			maxfieldlen = 10 * maxfieldlen + (fmt[i] - '0');
			i++;
		}
		if (maxfieldlen == 0 && fmt[i - 1] == '0') {
			maxfieldlen = (varlen + nvarlenfields - 1) /
			    nvarlenfields;
			if (maxfieldlen == 0)
				continue;
			varlen -= maxfieldlen;
			nvarlenfields--;
		}

		if ((field = format_get_field(fmt[i], fields, nfields)) ==
		    NULL) {
			buf[0] = '\0';
			return -1;
		}
		fieldlen = strlen(field);

		if (maxfieldlen == 0)
			maxfieldlen = fieldlen;
		else if (maxfieldlen < fieldlen)
			fieldlen = maxfieldlen;

		if (len + fieldlen > maxlen)
			fieldlen = maxlen - len;

		padlen = maxfieldlen - fieldlen;
		if (len + fieldlen + padlen > maxlen)
			padlen = maxlen - len - fieldlen;

		if (align == FORMAT_ALIGN_RIGHT)
			for (j = 0; j < padlen; j++)
				buf[len++] = ' ';

		for (j = 0; j < fieldlen; j++)
			buf[len++] = field[j];

		if (align == FORMAT_ALIGN_LEFT)
			for (j = 0; j < padlen; j++)
				buf[len++] = ' ';
	}

	buf[len] = '\0';
	return 0;
}
