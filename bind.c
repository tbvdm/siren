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

#ifdef __OpenBSD__
#include <sys/tree.h>
#else
#include "compat/tree.h"
#endif

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "siren.h"

/*
 * The length of the longest key-name (including "\0"). Most key names are
 * defined in the bind_key_names array below. The other key names have a length
 * of 1 (e.g. "a") or 2 (e.g. "^A").
 */
#define BIND_KEY_MAXLEN 10

struct bind_entry {
	int			 key;
	enum bind_scope		 scope;
	struct command		*command;
	void			*command_data;
	char			*command_string;
	RB_ENTRY(bind_entry)	 entries;
};

RB_HEAD(bind_tree, bind_entry);

static int			 bind_cmp_entry(struct bind_entry *,
				    struct bind_entry *);
static struct bind_entry	*bind_find(enum bind_scope, int);
static char			*bind_key_to_string(int, char *, size_t);
static void			 bind_remove(struct bind_entry *);
static const char		*bind_scope_to_string(enum bind_scope);

RB_PROTOTYPE(bind_tree, bind_entry, entries, bind_cmp_entry)

static struct bind_tree bind_tree = RB_INITIALIZER(bind_tree);

static const struct {
	const enum bind_scope	 scope;
	const char		*name;
} bind_scopes[] = {
	{ BIND_SCOPE_BROWSER,	"browser" },
	{ BIND_SCOPE_COMMON,	"common" },
	{ BIND_SCOPE_LIBRARY,	"library" },
	{ BIND_SCOPE_QUEUE,	"queue" }
};

static const struct {
	const int		 key;
	const char		*name;
} bind_keys[] = {
	{ ' ',			"space" },
	{ K_BACKSPACE,		"backspace" },
	{ K_BACKTAB,		"backtab" },
	{ K_DELETE,		"delete" },
	{ K_DOWN,		"down" },
	{ K_END,		"end" },
	{ K_ENTER,		"enter" },
	{ K_ESCAPE,		"escape" },
	{ K_HOME,		"home" },
	{ K_INSERT,		"insert" },
	{ K_LEFT,		"left" },
	{ K_PAGEDOWN,		"page-down" },
	{ K_PAGEUP,		"page-up" },
	{ K_RIGHT,		"right" },
	{ K_TAB,		"tab" },
	{ K_UP,			"up" },
	{ K_F1,			"f1" },
	{ K_F2,			"f2" },
	{ K_F3,			"f3" },
	{ K_F4,			"f4" },
	{ K_F5,			"f5" },
	{ K_F6,			"f6" },
	{ K_F7,			"f7" },
	{ K_F8,			"f8" },
	{ K_F9,			"f9" },
	{ K_F10,		"f10" },
	{ K_F11,		"f11" },
	{ K_F12,		"f12" },
	{ K_F13,		"f13" },
	{ K_F14,		"f14" },
	{ K_F15,		"f15" },
	{ K_F16,		"f16" },
	{ K_F17,		"f17" },
	{ K_F18,		"f18" },
	{ K_F19,		"f19" },
	{ K_F20,		"f20" }
};

RB_GENERATE(bind_tree, bind_entry, entries, bind_cmp_entry)

static void
bind_add(enum bind_scope scope, int key, const char *command)
{
	struct bind_entry	*b;
	char			*error, keyname[BIND_KEY_MAXLEN];

	b = xmalloc(sizeof *b);

	if (command_parse_string(command, &b->command, &b->command_data,
	    &error))
		LOG_FATALX("scope %s, key %s: invalid command: \"%s\": %s",
		    bind_scope_to_string(scope),
		    bind_key_to_string(key, keyname, sizeof keyname), command,
		    error);

	b->command_string = xstrdup(command);
	b->scope = scope;
	b->key = key;

	if (RB_INSERT(bind_tree, &bind_tree, b) != NULL)
		LOG_FATALX("scope %s, key %s: already bound",
		    bind_scope_to_string(scope),
		    bind_key_to_string(key, keyname, sizeof keyname));
}

static int
bind_cmp_entry(struct bind_entry *b1, struct bind_entry *b2)
{
	if (b1->scope != b2->scope)
		return b1->scope < b2->scope ? -1 : 1;
	else
		return b1->key < b2->key ? -1 : b1->key > b2->key;
}

void
bind_end(void)
{
	struct bind_entry *b;

	while ((b = RB_ROOT(&bind_tree)) != NULL)
		bind_remove(b);
}

int
bind_execute(enum bind_scope scope, int key)
{
	struct bind_entry *b;

	if ((b = bind_find(scope, key)) == NULL)
		return -1;

	command_execute(b->command, b->command_data);
	return 0;
}

static struct bind_entry *
bind_find(enum bind_scope scope, int key)
{
	struct bind_entry b;

	b.key = key;
	b.scope = scope;
	return RB_FIND(bind_tree, &bind_tree, &b);
}

const char *
bind_get_command(enum bind_scope scope, int key)
{
	struct bind_entry *b;

	if ((b = bind_find(scope, key)) == NULL)
		return NULL;
	else
		return b->command_string;
}

void
bind_init(void)
{
	bind_add(BIND_SCOPE_COMMON, K_CTRL('B'), "scroll-up -p");
	bind_add(BIND_SCOPE_COMMON, K_CTRL('D'), "scroll-down -h");
	bind_add(BIND_SCOPE_COMMON, K_CTRL('E'), "scroll-down -l");
	bind_add(BIND_SCOPE_COMMON, K_CTRL('F'), "scroll-down -p");
	bind_add(BIND_SCOPE_COMMON, K_CTRL('L'), "refresh-screen");
	bind_add(BIND_SCOPE_COMMON, K_CTRL('U'), "scroll-up -h");
	bind_add(BIND_SCOPE_COMMON, K_CTRL('Y'), "scroll-up -l");
	bind_add(BIND_SCOPE_COMMON, K_DOWN, "select-next-entry");
	bind_add(BIND_SCOPE_COMMON, K_END, "select-last-entry");
	bind_add(BIND_SCOPE_COMMON, K_ENTER, "activate-entry");
	bind_add(BIND_SCOPE_COMMON, K_HOME, "select-first-entry");
	bind_add(BIND_SCOPE_COMMON, K_LEFT, "seek -b 5");
	bind_add(BIND_SCOPE_COMMON, K_PAGEDOWN, "scroll-down -p");
	bind_add(BIND_SCOPE_COMMON, K_PAGEUP, "scroll-up -p");
	bind_add(BIND_SCOPE_COMMON, K_RIGHT, "seek -f 5");
	bind_add(BIND_SCOPE_COMMON, K_UP, "select-prev-entry");
	bind_add(BIND_SCOPE_COMMON, '+', "set-volume -i 10");
	bind_add(BIND_SCOPE_COMMON, ',', "seek -b 1:00");
	bind_add(BIND_SCOPE_COMMON, '.', "seek -f 1:00");
	bind_add(BIND_SCOPE_COMMON, '-', "set-volume -d 5");
	bind_add(BIND_SCOPE_COMMON, '/', "search-prompt");
	bind_add(BIND_SCOPE_COMMON, '<', "seek -b 5:00");
	bind_add(BIND_SCOPE_COMMON, '>', "seek -f 5:00");
	bind_add(BIND_SCOPE_COMMON, '?', "search-prompt -b");
	bind_add(BIND_SCOPE_COMMON, '1', "select-view library");
	bind_add(BIND_SCOPE_COMMON, '2', "select-view playlist");
	bind_add(BIND_SCOPE_COMMON, '3', "select-view browser");
	bind_add(BIND_SCOPE_COMMON, '4', "select-view queue");
	bind_add(BIND_SCOPE_COMMON, ':', "command-prompt");
	bind_add(BIND_SCOPE_COMMON, '=', "set-volume -i 5");
	bind_add(BIND_SCOPE_COMMON, 'C', "set continue");
	bind_add(BIND_SCOPE_COMMON, 'G', "select-last-entry");
	bind_add(BIND_SCOPE_COMMON, 'N', "search-prev");
	bind_add(BIND_SCOPE_COMMON, 'R', "set repeat-all");
	bind_add(BIND_SCOPE_COMMON, '_', "set-volume -d 10");
	bind_add(BIND_SCOPE_COMMON, 'b', "play-next");
	bind_add(BIND_SCOPE_COMMON, 'c', "pause");
	bind_add(BIND_SCOPE_COMMON, 'g', "select-first-entry");
	bind_add(BIND_SCOPE_COMMON, 'j', "select-next-entry");
	bind_add(BIND_SCOPE_COMMON, 'k', "select-prev-entry");
	bind_add(BIND_SCOPE_COMMON, 'n', "search-next");
	bind_add(BIND_SCOPE_COMMON, 'p', "search-prev");
	bind_add(BIND_SCOPE_COMMON, 'q', "quit");
	bind_add(BIND_SCOPE_COMMON, 'r', "set repeat-track");
	bind_add(BIND_SCOPE_COMMON, 'v', "stop");
	bind_add(BIND_SCOPE_COMMON, 'x', "play");
	bind_add(BIND_SCOPE_COMMON, 'z', "play-prev");

	bind_add(BIND_SCOPE_LIBRARY, K_DELETE, "delete-entry");
	bind_add(BIND_SCOPE_LIBRARY, 'd', "delete-entry");
	bind_add(BIND_SCOPE_LIBRARY, 'a', "add-entry -q");
	bind_add(BIND_SCOPE_LIBRARY, 'l', "delete-entry -a");

	bind_add(BIND_SCOPE_PLAYLIST, 'a', "add-entry -q");

	bind_add(BIND_SCOPE_QUEUE, K_DELETE, "delete-entry");
	bind_add(BIND_SCOPE_QUEUE, 'J', "move-entry-down");
	bind_add(BIND_SCOPE_QUEUE, 'K', "move-entry-up");
	bind_add(BIND_SCOPE_QUEUE, 'd', "delete-entry");
	bind_add(BIND_SCOPE_QUEUE, 'l', "delete-entry -a");

	bind_add(BIND_SCOPE_BROWSER, K_CTRL('R'), "reread-directory");
	bind_add(BIND_SCOPE_BROWSER, K_BACKSPACE, "cd ..");
	bind_add(BIND_SCOPE_BROWSER, 'a', "add-entry -q");
	bind_add(BIND_SCOPE_BROWSER, 'h', "set show-hidden-files");
}

static char *
bind_key_to_string(int key, char *name, size_t namelen)
{
	size_t i;

	if (K_IS_CTRL(key)) {
		xsnprintf(name, namelen, "^%c", K_UNCTRL(key));
		return name;
	}

	for (i = 0; i < NELEMENTS(bind_keys); i++)
		if (key == bind_keys[i].key) {
			strlcpy(name, bind_keys[i].name, namelen);
			return name;
		}

	xsnprintf(name, namelen, "%c", key);
	return name;
}

static void
bind_remove(struct bind_entry *b)
{
	RB_REMOVE(bind_tree, &bind_tree, b);
	command_free_data(b->command, b->command_data);
	free(b->command_string);
	free(b);
}

static const char *
bind_scope_to_string(enum bind_scope scope)
{
	size_t i;

	for (i = 0; i < NELEMENTS(bind_scopes); i++)
		if (scope == bind_scopes[i].scope)
			return bind_scopes[i].name;

	LOG_FATALX("unknown scope");
}

void
bind_set(enum bind_scope scope, int key, struct command *command,
    void *command_data, const char *command_string)
{
	struct bind_entry *b;

	if ((b = bind_find(scope, key)) != NULL)
		bind_remove(b);

	b = xmalloc(sizeof *b);
	b->scope = scope;
	b->key = key;
	b->command = command;
	b->command_data = command_data;
	b->command_string = xstrdup(command_string);
	RB_INSERT(bind_tree, &bind_tree, b);
}

int
bind_string_to_scope(const char *name, enum bind_scope *scope)
{
	size_t i;

	for (i = 0; i < NELEMENTS(bind_scopes); i++)
		if (!strcasecmp(name, bind_scopes[i].name)) {
			*scope = bind_scopes[i].scope;
			return 0;
		}

	return -1;
}

int
bind_string_to_key(const char *str)
{
	size_t i;

	/* Printable characters (ASCII 32 to 126 decimal). */
	if (str[0] >= ' ' && str[0] <= '~' && str[1] == '\0')
		return str[0];

	/* Control characters (ASCII 0 to 31 decimal, and 127 decimal). */
	if (str[0] == '^' && K_IS_CTRL(K_CTRL(toupper((unsigned char)str[1])))
	    && str[2] == '\0')
		return K_CTRL(toupper((unsigned char)str[1]));

	/* Key names. */
	for (i = 0; i < NELEMENTS(bind_keys); i++)
		if (!strcasecmp(str, bind_keys[i].name))
			return bind_keys[i].key;

	return K_NONE;
}

int
bind_unset(enum bind_scope scope, int key)
{
	struct bind_entry *b;

	if ((b = bind_find(scope, key)) == NULL)
		return -1;

	bind_remove(b);
	return 0;
}
