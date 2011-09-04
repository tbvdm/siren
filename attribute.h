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

/*
 * GCC attributes. See
 * <http://gcc.gnu.org/onlinedocs/gcc/Attribute-Syntax.html> and
 * <http://ohse.de/uwe/articles/gcc-attributes.html>.
 */

#ifndef ATTRIBUTE_H
#define ATTRIBUTE_H

#ifdef __GNUC__

/* The "format" attribute is supported since GCC 2.3. */
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 3)
#define PRINTFLIKE(fmt, arg)
#else
#define PRINTFLIKE(fmt, arg)	__attribute__((format(printf, fmt, arg)))
#endif

/* The "nonnull" attribute is supported since GCC 3.3. */
#if __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 3)
#define NONNULL(...)
#else
#define NONNULL(...)		__attribute__((nonnull(__VA_ARGS__)))
#endif

/* The "noreturn" attribute is supported since GCC 2.5. */
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 5)
#define NORETURN
#else
#define NORETURN		__attribute__((noreturn))
#endif

/* The "unused" attribute is supported since GCC 2.7. */
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 7)
#define UNUSED
#else
#define UNUSED			__attribute__((unused))
#endif

#endif

#endif
