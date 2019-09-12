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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "siren.h"

NORETURN static void
usage(void)
{
	fprintf(stderr, "usage: %s [-lv] [-c directory]\n", getprogname());
	exit(1);
}

NORETURN static void
version(void)
{
	printf("siren %s\n", VERSION);
	exit(0);
}

int
main(int argc, char **argv)
{
	int	 c, lflag;
	char	*confdir;
	char	*promises;

	confdir = NULL;
	lflag = 0;
	while ((c = getopt(argc, argv, "c:lv")) != -1)
		switch (c) {
		case 'c':
			confdir = optarg;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'v':
			version();
			break;
		default:
			usage();
			break;
		}

	if (argc != optind)
		usage();

	opterr = 0;

	log_init(lflag);
	input_init();
	option_init();
	bind_init();
	conf_init(confdir);
	screen_init();
	plugin_init();
	track_init();
	library_init();
	playlist_init();
	queue_init();
	browser_init();
	player_init();
	prompt_init();

	promises = xstrdup("stdio rpath wpath cpath getpw tty");
	plugin_append_promises(&promises);
	LOG_INFO("pledging %s", promises);

	if (pledge(promises, NULL) == -1)
		err(1, "pledge");

	free(promises);

	screen_print();
	conf_read_file();
	library_read_file();
	cache_update();
	input_handle_key();

	prompt_end();
	player_end();
	browser_end();
	queue_end();
	playlist_end();
	library_end();
	track_end();
	plugin_end();
	screen_end();
	conf_end();
	bind_end();
	option_end();
	log_end();

	return 0;
}
