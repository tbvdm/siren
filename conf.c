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

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

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
	char *home, *tmp;

	if (dir != NULL)
		conf_dir = path_normalise(dir);
	else {
		if ((home = path_get_home_dir(NULL)) == NULL)
			LOG_FATALX("cannot determine home directory");
		(void)xasprintf(&tmp, "%s/%s", home, CONF_DIR);
		conf_dir = path_normalise(tmp);
		free(home);
		free(tmp);
	}

	if (mkdir(conf_dir, S_IRWXU | S_IRWXG | S_IRWXO) == -1 &&
	    errno != EEXIST) {
		LOG_ERR("mkdir: %s", conf_dir);
		msg_err("Cannot create configuration directory: %s", conf_dir);
	}
}

char *
conf_path(const char *file)
{
	char *path;

	(void)xasprintf(&path, "%s/%s", conf_dir, file);
	return path;
}

/*
 * Read the configuration file.
 */
void
conf_read_file(void)
{
	FILE	*fp;
	size_t	 len, lineno;
	char	*buf, *error, *file, *lbuf;

	file = conf_path(CONF_FILE);
	if ((fp = fopen(file, "r")) == NULL) {
		if (errno != ENOENT) {
			LOG_ERR("fopen: %s", file);
			msg_err("Cannot open configuration file");
		}
		free(file);
		return;
	}

	lbuf = NULL;
	for (lineno = 1; (buf = fgetln(fp, &len)) != NULL; lineno++) {
		if (buf[len - 1] != '\n') {
			lbuf = xmalloc(len + 1);
			buf = memcpy(lbuf, buf, len++);
		}
		buf[len - 1] = '\0';

		if (command_process(buf, &error) == -1) {
			msg_errx("%s:%zu: %s", file, lineno, error);
			free(error);
		}
	}
	if (ferror(fp)) {
		LOG_ERR("fgetln: %s", buf);
		msg_err("Cannot read configuration file");
	}
	free(lbuf);
	free(file);

	(void)fclose(fp);
}
