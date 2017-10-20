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

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "siren.h"

static char *conf_dir;

void
conf_end(void)
{
	free(conf_dir);
}

void
conf_init(const char *dir)
{
	char *home;

	if (dir != NULL)
		conf_dir = path_normalise(dir);
	else {
		if ((home = path_get_home_dir(NULL)) == NULL)
			xasprintf(&conf_dir, "/%s", CONF_DIR);
		else {
			xasprintf(&conf_dir, "%s/%s", home, CONF_DIR);
			free(home);
		}
	}

	if (mkdir(conf_dir, S_IRWXU | S_IRWXG | S_IRWXO) == -1 &&
	    errno != EEXIST) {
		LOG_ERR("mkdir: %s", conf_dir);
		msg_err("Cannot create configuration directory: %s", conf_dir);
	}
}

char *
conf_get_path(const char *file)
{
	char *path;

	xasprintf(&path, "%s/%s", conf_dir, file);
	return path;
}

void
conf_read_file(void)
{
	char *file;

	file = conf_get_path(CONF_FILE);
	if (access(file, F_OK) == 0 || errno != ENOENT)
		conf_source_file(file);
	free(file);
}

void
conf_source_file(const char *file)
{
	FILE	*fp;
	size_t	 lineno, size;
	ssize_t	 len;
	char	*error, *line;

	if ((fp = fopen(file, "r")) == NULL) {
		LOG_ERR("fopen: %s", file);
		msg_err("Cannot open %s", file);
		return;
	}

	line = NULL;
	size = 0;
	for (lineno = 1; (len = getline(&line, &size, fp)) != -1; lineno++) {
		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';

		if (command_process(line, &error) == -1) {
			msg_errx("%s:%zu: %s", file, lineno, error);
			free(error);
		}
	}
	if (ferror(fp)) {
		LOG_ERR("getline: %s", file);
		msg_err("Cannot read configuration file");
	}
	free(line);

	fclose(fp);
}
