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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../siren.h"

#define FGETLN_BUFSIZE 1024

char *
fgetln(FILE *fp, size_t *buflen)
{
	static size_t	 bufsize = 0;
	static char	*buf = NULL;

	*buflen = 0;
	for (;;) {
		if (*buflen + 1 == bufsize || bufsize == 0) {
			bufsize += FGETLN_BUFSIZE;
			buf = xrealloc(buf, bufsize);
		}

		if (fgets(buf + *buflen, bufsize - *buflen, fp) == NULL) {
			if (*buflen == 0) {
				free(buf);
				buf = NULL;
				bufsize = 0;
			}
			break;
		}

		*buflen += strlen(buf + *buflen);
		if (*buflen && buf[*buflen - 1] == '\n')
			break;
	}

	return buf;
}
