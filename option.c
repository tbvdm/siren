/*
 * Copyright (c) 2011, 2012 Tim van der Molen <tbvdm@xs4all.nl>
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

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "siren.h"

#ifdef HAVE_TREE_H
#include <sys/tree.h>
#else
#include "compat/tree.h"
#endif

/*
 * Maximum size of any string created by option_attrib_to_string(). The longest
 * possible string is "blink,bold,dim,reverse,standout,underline", which
 * contains 42 characters (including the terminating NUL byte).
 */
#define OPTION_ATTRIB_MAXLEN 42

struct option_entry {
	char			*name;
	enum option_type	 type;
	union {
		struct {
			int		 cur;
			int		 min;
			int		 max;
		} number;

		struct format		*format;
		enum colour		 colour;
		int			 attrib;
		int			 boolean;
		char			*string;
	} value;
	void			 (*callback)(void);

	SPLAY_ENTRY(option_entry) entries;
};

SPLAY_HEAD(option_tree, option_entry);

static int			 option_cmp_entry(struct option_entry *,
				    struct option_entry *);
static void			 option_insert_entry(struct option_entry *);

SPLAY_PROTOTYPE(option_tree, option_entry, entries, option_cmp_entry)

static struct option_tree	option_tree = SPLAY_INITIALIZER(option_tree);
static pthread_mutex_t		option_tree_mtx = PTHREAD_MUTEX_INITIALIZER;

static const struct {
	const int		 attrib;
	const char		*name;
} option_attrib_names[] = {
	{ ATTRIB_BLINK,		"blink" },
	{ ATTRIB_BOLD,		"bold" },
	{ ATTRIB_DIM,		"dim" },
	{ ATTRIB_NORMAL,	"normal" },
	{ ATTRIB_REVERSE,	"reverse" },
	{ ATTRIB_STANDOUT,	"standout" },
	{ ATTRIB_UNDERLINE,	"underline" }
};

static const struct {
	const enum colour	 colour;
	const char		*name;
} option_colour_names[] = {
	{ COLOUR_BLACK,		"black" },
	{ COLOUR_BLUE,		"blue" },
	{ COLOUR_CYAN,		"cyan" },
	{ COLOUR_DEFAULT,	"default" },
	{ COLOUR_GREEN,		"green" },
	{ COLOUR_MAGENTA,	"magenta" },
	{ COLOUR_RED,		"red" },
	{ COLOUR_WHITE,		"white" },
	{ COLOUR_YELLOW,	"yellow" }
};

static const struct {
	const int		 boolean;
	const char		*name;
} option_boolean_names[] = {
	{ 0,			"false" },
	{ 1,			"true" },
	/* Aliases for the above two names. */
	{ 0,			"0" },
	{ 0,			"off" },
	{ 0,			"no" },
	{ 1,			"1" },
	{ 1,			"on" },
	{ 1,			"yes" }
};

SPLAY_GENERATE(option_tree, option_entry, entries, option_cmp_entry)

static void
option_add_attrib(const char *name, int value, void (*callback)(void))
{
	struct option_entry *o;

	o = xmalloc(sizeof *o);
	o->name = xstrdup(name);
	o->type = OPTION_TYPE_ATTRIB;
	o->value.attrib = value;
	o->callback = callback;
	option_insert_entry(o);
}

static void
option_add_boolean(const char *name, int value, void (*callback)(void))
{
	struct option_entry *o;

	o = xmalloc(sizeof *o);
	o->name = xstrdup(name);
	o->type = OPTION_TYPE_BOOLEAN;
	o->value.boolean = value;
	o->callback = callback;
	option_insert_entry(o);
}

static void
option_add_colour(const char *name, enum colour value, void (*callback)(void))
{
	struct option_entry *o;

	o = xmalloc(sizeof *o);
	o->name = xstrdup(name);
	o->type = OPTION_TYPE_COLOUR;
	o->value.colour = value;
	o->callback = callback;
	option_insert_entry(o);
}

static void
option_add_format(const char *name, const char *fmt, void (*callback)(void))
{
	struct option_entry *o;

	o = xmalloc(sizeof *o);
	o->name = xstrdup(name);
	o->type = OPTION_TYPE_FORMAT;
	o->value.format = format_parse(fmt);
	o->callback = callback;
	option_insert_entry(o);
}

void
option_add_number(const char *name, int value, int minvalue, int maxvalue,
    void (*callback)(void))
{
	struct option_entry *o;

#ifdef DEBUG
	if (minvalue > maxvalue)
		LOG_FATALX("%s: minimum value larger than maximum value",
		    name);
	if (value < minvalue || value > maxvalue)
		LOG_FATALX("%s: initial value not within range", name);
#endif

	o = xmalloc(sizeof *o);
	o->name = xstrdup(name);
	o->type = OPTION_TYPE_NUMBER;
	o->value.number.cur = value;
	o->value.number.min = minvalue;
	o->value.number.max = maxvalue;
	o->callback = callback;
	option_insert_entry(o);
}

void
option_add_string(const char *name, const char *value, void (*callback)(void))
{
	struct option_entry *o;

	o = xmalloc(sizeof *o);
	o->name = xstrdup(name);
	o->type = OPTION_TYPE_STRING;
	o->value.string = xstrdup(value);
	o->callback = callback;
	option_insert_entry(o);
}

char *
option_attrib_to_string(int attrib)
{
	size_t	i;
	char	str[OPTION_ATTRIB_MAXLEN];

	str[0] = '\0';
	for (i = 0; i < NELEMENTS(option_attrib_names); i++)
		if (attrib & option_attrib_names[i].attrib ||
		    attrib == option_attrib_names[i].attrib) {
			if (str[0] != '\0')
				(void)strlcat(str, ",", sizeof str);
			(void)strlcat(str, option_attrib_names[i].name,
			    sizeof str);
		}

	return xstrdup(str);
}

const char *
option_boolean_to_string(int boolean)
{
	size_t i;

	for (i = 0; i < NELEMENTS(option_boolean_names); i++)
		if (boolean == option_boolean_names[i].boolean)
			return option_boolean_names[i].name;

	LOG_FATALX("unknown boolean");
	/* NOTREACHED */
}

static int
option_cmp_entry(struct option_entry *o1, struct option_entry *o2)
{
	return strcmp(o1->name, o2->name);
}

const char *
option_colour_to_string(enum colour colour)
{
	size_t i;

	for (i = 0; i < NELEMENTS(option_colour_names); i++)
		if (colour == option_colour_names[i].colour)
			return option_colour_names[i].name;

	LOG_FATALX("unknown colour");
	/* NOTREACHED */
}

void
option_end(void)
{
	struct option_entry *o;

	while ((o = SPLAY_ROOT(&option_tree)) != NULL) {
		(void)SPLAY_REMOVE(option_tree, &option_tree, o);
		free(o->name);
		switch (o->type) {
		case OPTION_TYPE_FORMAT:
			format_free(o->value.format);
			break;
		case OPTION_TYPE_STRING:
			free(o->value.string);
			break;
		/* Silence gcc. */
		default:
			break;
		}
		free(o);
	}
}

/*
 * The option_tree_mtx mutex must be locked before calling this function.
 */
static struct option_entry *
option_find(const char *name)
{
	struct option_entry find, *o;

	find.name = xstrdup(name);
	o = SPLAY_FIND(option_tree, &option_tree, &find);
	free(find.name);
	return o;
}

/*
 * The option_tree_mtx mutex must be locked before calling this function.
 */
static struct option_entry *
option_find_type(const char *name, enum option_type type)
{
	struct option_entry *o;

	if ((o = option_find(name)) == NULL)
		LOG_FATALX("%s: option does not exist", name);
	if (o->type != type)
		LOG_FATALX("%s: option is not of expected type", name);
	return o;
}

const char *
option_format_to_string(const struct format *format)
{
	return format_to_string(format);
}

int
option_get_attrib(const char *name)
{
	struct option_entry	*o;
	int			 attrib;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_ATTRIB);
	attrib = o->value.attrib;
	XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	return attrib;
}

int
option_get_boolean(const char *name)
{
	struct option_entry	*o;
	int			 boolean;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_BOOLEAN);
	boolean = o->value.boolean;
	XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	return boolean;
}

enum colour
option_get_colour(const char *name)
{
	struct option_entry	*o;
	enum colour		 colour;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_COLOUR);
	colour = o->value.colour;
	XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	return colour;
}

/*
 * option_lock() must be called before calling this function, and
 * option_unlock() must be called afterwards.
 */
struct format *
option_get_format(const char *name)
{
	struct option_entry	*o;
	struct format		*format;

	o = option_find_type(name, OPTION_TYPE_FORMAT);
	format = o->value.format;
	return format;
}

int
option_get_number(const char *name)
{
	struct option_entry	*o;
	int			 number;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_NUMBER);
	number = o->value.number.cur;
	XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	return number;
}

void
option_get_number_range(const char *name, int *min, int *max)
{
	struct option_entry *o;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_NUMBER);
	*min = o->value.number.min;
	*max = o->value.number.max;
	XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
}

char *
option_get_string(const char *name)
{
	struct option_entry	*o;
	char			*string;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_STRING);
	string = xstrdup(o->value.string);
	XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	return string;
}

int
option_get_type(const char *name, enum option_type *type)
{
	struct option_entry	*o;
	int			 ret;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	if ((o = option_find(name)) == NULL)
		ret = -1;
	else {
		*type = o->type;
		ret = 0;
	}
	XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	return ret;
}

void
option_init(void)
{
	option_add_boolean("continue", 1, player_print);
	option_add_number("max-history-entries", 100, 0, INT_MAX,
	    prompt_resize_histories);
	option_add_format("library-format", "%-*a %-*l %4y %2n. %-*t %5d",
	    library_print);
	option_add_string("output-plugin", "default", player_change_op);
	option_add_format("player-status-format",
	    "%-7s  %5p / %5d  %3v%%%{?c,  continue,}%{?r,  repeat-all,}"
	    "%{?t,  repeat-track,}", player_print);
	option_add_format("player-track-format", "%a - %l (%y) - %n. %t",
	    player_print);
	option_add_format("queue-format", "%-*a %-*t %5d", queue_print);
	option_add_boolean("repeat-all", 1, player_print);
	option_add_boolean("repeat-track", 0, player_print);
	option_add_boolean("show-all-files", 0, browser_refresh_dir);
	option_add_boolean("show-cursor", 0, screen_configure_cursor);
	option_add_boolean("show-dirs-before-files", 0, browser_refresh_dir);
	option_add_boolean("show-hidden-files", 0, browser_refresh_dir);

	option_add_attrib("active-attr", ATTRIB_NORMAL,
	    screen_configure_objects);
	option_add_colour("active-bg", COLOUR_DEFAULT,
	    screen_configure_objects);
	option_add_colour("active-fg", COLOUR_YELLOW,
	    screen_configure_objects);
	option_add_attrib("error-attr", ATTRIB_NORMAL,
	    screen_configure_objects);
	option_add_colour("error-bg", COLOUR_DEFAULT,
	    screen_configure_objects);
	option_add_colour("error-fg", COLOUR_RED, screen_configure_objects);
	option_add_attrib("info-attr", ATTRIB_NORMAL,
	    screen_configure_objects);
	option_add_colour("info-bg", COLOUR_DEFAULT, screen_configure_objects);
	option_add_colour("info-fg", COLOUR_CYAN, screen_configure_objects);
	option_add_attrib("player-attr", ATTRIB_REVERSE,
	    screen_configure_objects);
	option_add_colour("player-bg", COLOUR_DEFAULT,
	    screen_configure_objects);
	option_add_colour("player-fg", COLOUR_DEFAULT,
	    screen_configure_objects);
	option_add_attrib("prompt-attr", ATTRIB_NORMAL,
	    screen_configure_objects);
	option_add_colour("prompt-bg", COLOUR_DEFAULT,
	    screen_configure_objects);
	option_add_colour("prompt-fg", COLOUR_DEFAULT,
	    screen_configure_objects);
	option_add_attrib("selection-attr", ATTRIB_REVERSE,
	    screen_configure_objects);
	option_add_colour("selection-bg", COLOUR_WHITE,
	    screen_configure_objects);
	option_add_colour("selection-fg", COLOUR_BLUE,
	    screen_configure_objects);
	option_add_attrib("status-attr", ATTRIB_NORMAL,
	    screen_configure_objects);
	option_add_colour("status-bg", COLOUR_DEFAULT,
	    screen_configure_objects);
	option_add_colour("status-fg", COLOUR_DEFAULT,
	    screen_configure_objects);
	option_add_attrib("view-attr", ATTRIB_NORMAL,
	    screen_configure_objects);
	option_add_colour("view-bg", COLOUR_DEFAULT, screen_configure_objects);
	option_add_colour("view-fg", COLOUR_DEFAULT, screen_configure_objects);
	option_add_attrib("view-title-attr", ATTRIB_REVERSE,
	    screen_configure_objects);
	option_add_colour("view-title-bg", COLOUR_DEFAULT,
	    screen_configure_objects);
	option_add_colour("view-title-fg", COLOUR_DEFAULT,
	    screen_configure_objects);
}

static void
option_insert_entry(struct option_entry *o)
{
	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	if (SPLAY_INSERT(option_tree, &option_tree, o) != NULL)
		LOG_FATALX("%s: option already exists", o->name);
	XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
}

void
option_lock(void)
{
	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
}

void
option_set_attrib(const char *name, int value)
{
	struct option_entry *o;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_ATTRIB);
	if (value == o->value.attrib)
		XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	else {
		o->value.attrib = value;
		XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
		if (o->callback != NULL)
			o->callback();
	}
}

void
option_set_boolean(const char *name, int value)
{
	struct option_entry *o;

#ifdef DEBUG
	if (value != 0 && value != 1)
		LOG_FATALX("%s: %d: invalid value", name, value);
#endif

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_BOOLEAN);
	if (value == o->value.boolean)
		XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	else {
		o->value.boolean = value;
		XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
		if (o->callback != NULL)
			o->callback();
	}
}

void
option_set_colour(const char *name, enum colour value)
{
	struct option_entry *o;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_COLOUR);
	if (value == o->value.colour)
		XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	else {
		o->value.colour = value;
		XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
		if (o->callback != NULL)
			o->callback();
	}
}

void
option_set_format(const char *name, struct format *format)
{
	struct option_entry *o;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_FORMAT);
	format_free(o->value.format);
	o->value.format = format;
	XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	if (o->callback != NULL)
		o->callback();
}

void
option_set_number(const char *name, int value)
{
	struct option_entry *o;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_NUMBER);
	if (value == o->value.number.cur || value < o->value.number.min ||
	    value > o->value.number.max)
		XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	else {
		o->value.number.cur = value;
		XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
		if (o->callback != NULL)
			o->callback();
	}
}

void
option_set_string(const char *name, const char *value)
{
	struct option_entry *o;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_STRING);
	if (!strcmp(value, o->value.string))
		XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	else {
		free(o->value.string);
		o->value.string = xstrdup(value);
		XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
		if (o->callback != NULL)
			o->callback();
	}
}

int
option_string_to_attrib(const char *name)
{
	size_t i;

	for (i = 0; i < NELEMENTS(option_attrib_names); i++)
		if (!strcasecmp(name, option_attrib_names[i].name))
			return option_attrib_names[i].attrib;
	return -1;
}

int
option_string_to_boolean(const char *name)
{
	size_t i;

	for (i = 0; i < NELEMENTS(option_boolean_names); i++)
		if (!strcasecmp(name, option_boolean_names[i].name))
			return option_boolean_names[i].boolean;
	return -1;
}

int
option_string_to_colour(const char *name, enum colour *colour)
{
	size_t i;

	for (i = 0; i < NELEMENTS(option_colour_names); i++)
		if (!strcasecmp(name, option_colour_names[i].name)) {
			*colour = option_colour_names[i].colour;
			return 0;
		}
	return -1;
}

void
option_toggle_boolean(const char *name)
{
	struct option_entry *o;

	XPTHREAD_MUTEX_LOCK(&option_tree_mtx);
	o = option_find_type(name, OPTION_TYPE_BOOLEAN);
	o->value.boolean = !o->value.boolean;
	XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
	if (o->callback != NULL)
		o->callback();
}

void
option_unlock(void)
{
	XPTHREAD_MUTEX_UNLOCK(&option_tree_mtx);
}
