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
 * GCC attributes. See
 * <https://gcc.gnu.org/onlinedocs/gcc/Attribute-Syntax.html> and
 * <https://ohse.de/uwe/articles/gcc-attributes.html>.
 */

#ifndef ATTRIBUTE_H
#define ATTRIBUTE_H

/* Check if the GCC version is equal to or greater than the one specified. */
#define GCC_VERSION(major, minor)					\
	(defined(__GNUC__) && (__GNUC__ > major ||			\
	(__GNUC__ == major && __GNUC_MINOR__ >= minor)))

#if GCC_VERSION(3, 3) || defined(__clang__)
#define NONNULL(...)		__attribute__((nonnull(__VA_ARGS__)))
#else
#define NONNULL(...)
#endif

#if GCC_VERSION(2, 5) || defined(__clang__)
#define NORETURN		__attribute__((noreturn))
#else
#define NORETURN
#endif

#if GCC_VERSION(2, 3) || defined(__clang__)
#define PRINTFLIKE(fmt, arg)	__attribute__((format(printf, fmt, arg)))
#else
#define PRINTFLIKE(fmt, arg)
#endif

#if GCC_VERSION(2, 7) || defined(__clang__)
#define UNUSED			__attribute__((unused))
#else
#define UNUSED
#endif

#undef GCC_VERSION

#endif
