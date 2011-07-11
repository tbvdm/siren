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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "siren.h"

#define CACHE_FNV_BASIS	2166136261U
#define CACHE_FNV_PRIME	16777619U

static long int	 cache_find_pointer(const char *);
static void	 cache_handle_error(void);
static uint32_t	 cache_hash(const char *);
static int	 cache_read(FILE *, void *, size_t);
static int	 cache_read_string(char **);
static int	 cache_seek(FILE *, long int, int);
static void	 cache_update_pointer(long int, long int);
static int	 cache_write(FILE *, const void *, size_t);
static void	 cache_write_index(const char *, long int);
static long int  cache_write_record(const struct track *);

int		 cache_error;
FILE		*cache_dat_fp = NULL;
FILE		*cache_idx_fp = NULL;
char		*cache_dat_file;
char		*cache_idx_file;

void
cache_add_metadata(const struct track *t)
{
	long int	 ptr, nptr;
	int		 ret;
	char		*path;

	if (cache_error)
		return;

	/* Check if there is a chain for the hash value of the path. */
	if ((ptr = cache_find_pointer(t->path)) == -1) {
		/* There is no chain: simply write the record and its index. */
		if ((ptr = cache_write_record(t)) != -1)
			cache_write_index(t->path, ptr);
		return;
	}

	/*
	 * Find the last the record in the chain and append the new record to
	 * it.
	 */
	for (;;) {
		/* Seek to the next record in the chain. */
		if (cache_seek(cache_dat_fp, ptr, SEEK_SET))
			return;

		/* Read the pointer to the next record in the chain. */
		if (cache_read(cache_dat_fp, &nptr, sizeof nptr))
			return;

		if (cache_read_string(&path) || path == NULL)
			return;

		ret = strcmp(t->path, path);
		free(path);
		if (ret == 0) {
			/* This should not happen. */
			LOG_ERRX("%s: track is already in cache", t->path);
			return;
		}

		if (nptr == 0)
			/* We have reached the last record in the chain. */
			break;

		ptr = nptr;
	}

	/*
	 * Write the new record and update the previous record in the chain so
	 * that it points to the new record.
	 */
	if ((nptr = cache_write_record(t)) != -1)
		cache_update_pointer(ptr, nptr);
}

static void
cache_close_files(void)
{
	if (cache_dat_fp != NULL)
		(void)fclose(cache_dat_fp);
	if (cache_idx_fp != NULL)
		(void)fclose(cache_idx_fp);
}

void
cache_end(void)
{
	if (!cache_error)
		cache_close_files();
}

/*
 * Search the index file for the pointer that belongs to the hash value of
 * path.
 */
static long int
cache_find_pointer(const char *path)
{
	long int	ptr;
	uint32_t	hash, idxhash;

	if (cache_seek(cache_idx_fp, 0, SEEK_SET))
		return -1;

	hash = cache_hash(path);
	for (;;) {
		if (cache_read(cache_idx_fp, &idxhash, sizeof idxhash))
			return -1;

		if (hash == idxhash) {
			/* The hashes match: read and return the pointer. */
			if (cache_read(cache_idx_fp, &ptr, sizeof ptr))
				return -1;
			return ptr;
		}

		/* Seek to the next hash. */
		if (cache_seek(cache_idx_fp, sizeof(long int), SEEK_CUR))
			return -1;
	}
}

int
cache_get_metadata(struct track *t)
{
	long int	 ptr;
	int		 ret;
	char		*path;

	if (cache_error)
		return -1;

	if ((ptr = cache_find_pointer(t->path)) == -1)
		return -1;

	for (;;) {
		/* Seek to the next record in the chain. */
		if (cache_seek(cache_dat_fp, ptr, SEEK_SET))
			return -1;

		/* Read the pointer to the next record in the chain. */
		if (cache_read(cache_dat_fp, &ptr, sizeof ptr))
			return -1;

		if (cache_read_string(&path) == -1)
			return -1;

		if (path != NULL) {
			ret = strcmp(t->path, path);
			free(path);
			if (ret == 0)
				/* We have found the right record. */
				break;
		}

		if (ptr == 0)
			/* We have reached the last record in the chain. */
			return -1;
	}

	/* Read the metadata. */
	if (cache_read_string(&t->artist) ||
	    cache_read_string(&t->album) ||
	    cache_read_string(&t->date) ||
	    cache_read_string(&t->track) ||
	    cache_read_string(&t->title) ||
	    cache_read_string(&t->genre) ||
	    cache_read(cache_dat_fp, &t->duration, sizeof t->duration)) {
		free(t->artist);
		free(t->album);
		free(t->date);
		free(t->track);
		free(t->title);
		free(t->genre);
		return -1;
	}

	return 0;
}

static void
cache_handle_error(void)
{
	cache_close_files();
	cache_error = 1;
}

/*
 * Compute a 32-bit hash value using the Fowler-Noll-Vo hash algorithm. See
 * <http://www.isthe.com/chongo/tech/comp/fnv/>.
 */
static uint32_t
cache_hash(const char *s)
{
	uint32_t hash;

	hash = CACHE_FNV_BASIS;
	while (*s != '\0') {
		hash *= CACHE_FNV_PRIME;
		hash ^= *s++;
	}

	return hash;
}

void
cache_init(void)
{
	const char *mode;

	cache_dat_file = conf_path(CACHE_DAT_FILE);
	cache_idx_file = conf_path(CACHE_IDX_FILE);

	if (access(cache_dat_file, F_OK) || access(cache_idx_file, F_OK))
		mode = "w+";
	else
		mode = "r+";

	if ((cache_dat_fp = fopen(cache_dat_file, mode)) == NULL) {
		LOG_ERR("fopen: %s", cache_dat_file);
		cache_handle_error();
		return;
	}

	if ((cache_idx_fp = fopen(cache_idx_file, mode)) == NULL) {
		LOG_ERR("fopen: %s", cache_idx_file);
		cache_handle_error();
	}
}

static int
cache_read(FILE *fp, void *obj, size_t objsize)
{
	if (fread(obj, objsize, 1, fp) != 1) {
		if (ferror(fp)) {
			LOG_ERR("fread");
			cache_handle_error();
		}

		return -1;
	}

	return 0;
}

static int
cache_read_string(char **str)
{
	size_t len;

	/* Read the length of the string. */
	if (cache_read(cache_dat_fp, &len, sizeof len) == -1)
		return -1;

	if (len == SIZE_MAX)
		return -1;

	if (len == 0) {
		/* The string is empty. */
		*str = NULL;
		return 0;
	}

	/* Read the string. */
	*str = xmalloc(len + 1);
	if (cache_read(cache_dat_fp, *str, len) == -1)
		return -1;

	(*str)[len] = '\0';
	return 0;
}

static int
cache_seek(FILE *fp, long int offset, int whence)
{
	if (fseek(fp, offset, whence)) {
		LOG_ERR("fseek");
		cache_handle_error();
		return -1;
	}

	return 0;
}

/*
 * Update the pointer to the next record for the record at the specified
 * offset.
 */
static void
cache_update_pointer(long int offset, long int ptr)
{
	if (cache_seek(cache_dat_fp, offset, SEEK_SET) == -1)
		return;

	(void)cache_write(cache_dat_fp, &ptr, sizeof ptr);
}

static int
cache_write(FILE *fp, const void *obj, size_t objsize)
{
	if (fwrite(obj, objsize, 1, fp) != 1) {
		LOG_ERR("fwrite");
		cache_handle_error();
		return -1;
	}

	return 0;
}

static int
cache_write_string(const char *str)
{
	size_t len;

	/* Determine the length of the string and write it. */
	len = str == NULL ? 0 : strlen(str);
	if (cache_write(cache_dat_fp, &len, sizeof len))
		return -1;

	/* Write the string if it is non-empty. */
	if (len && cache_write(cache_dat_fp, str, len))
		return -1;

	return 0;
}

/* Write an index entry to the index file. */
static void
cache_write_index(const char *path, long int ptr)
{
	uint32_t hash;

	if (cache_seek(cache_idx_fp, 0, SEEK_END))
		return;

	hash = cache_hash(path);
	(void)cache_write(cache_idx_fp, &hash, sizeof hash);
	(void)cache_write(cache_idx_fp, &ptr, sizeof ptr);
}

static long int
cache_write_record(const struct track *t)
{
	long int	 offset, ptr;

	if (cache_seek(cache_dat_fp, 0, SEEK_END) == -1)
		return -1;

	if ((offset = ftell(cache_dat_fp)) < 0) {
		LOG_ERR("ftell: %s", cache_dat_file);
		cache_handle_error();
		return -1;
	}

	ptr = 0;
	if (cache_write(cache_dat_fp, &ptr, sizeof ptr) ||
	    cache_write_string(t->path) ||
	    cache_write_string(t->artist) ||
	    cache_write_string(t->album) ||
	    cache_write_string(t->date) ||
	    cache_write_string(t->track) ||
	    cache_write_string(t->title) ||
	    cache_write_string(t->genre) ||
	    cache_write(cache_dat_fp, &t->duration, sizeof t->duration))
		return -1;

	return offset;
}
