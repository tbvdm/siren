/*
 * Written by Tim van der Molen.
 * Public domain.
 */

/* Ensure that glibc exposes the POSIX version of strerror_r(). */
#undef _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L

#include <string.h>

int
xstrerror_r(int errnum, char *buf, size_t bufsize)
{
	return strerror_r(errnum, buf, bufsize);
}
