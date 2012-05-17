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

#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "siren.h"

#ifdef HAVE_QUEUE_H
#include <sys/queue.h>
#else
#include "compat/queue.h"
#endif

struct plugin_ip_entry {
	void		*handle;
	const struct ip	*ip;
	SLIST_ENTRY(plugin_ip_entry) entries;
};

struct plugin_op_entry {
	void		*handle;
	const struct op	*op;
	SLIST_ENTRY(plugin_op_entry) entries;
};

static void plugin_load_dir(const char *, const char *,
    void (*)(void *, void *));

static SLIST_HEAD(, plugin_ip_entry) plugin_ip_list =
    SLIST_HEAD_INITIALIZER(plugin_ip_list);
static SLIST_HEAD(, plugin_op_entry) plugin_op_list =
    SLIST_HEAD_INITIALIZER(plugin_op_list);

static void
plugin_add_ip(void *handle, void *ip)
{
	struct plugin_ip_entry *ipe;

	ipe = xmalloc(sizeof *ipe);
	ipe->handle = handle;
	ipe->ip = ip;
	SLIST_INSERT_HEAD(&plugin_ip_list, ipe, entries);
}

static void
plugin_add_op(void *handle, void *op)
{
	struct plugin_op_entry *ope;

	ope = xmalloc(sizeof *ope);
	ope->handle = handle;
	ope->op = op;
	SLIST_INSERT_HEAD(&plugin_op_list, ope, entries);

	if (ope->op->init != NULL)
		ope->op->init();
}

void
plugin_end(void)
{
	struct plugin_ip_entry *ipe;
	struct plugin_op_entry *ope;

	while ((ipe = SLIST_FIRST(&plugin_ip_list)) != NULL) {
		SLIST_REMOVE_HEAD(&plugin_ip_list, entries);
		(void)dlclose(ipe->handle);
		free(ipe);
	}

	while ((ope = SLIST_FIRST(&plugin_op_list)) != NULL) {
		SLIST_REMOVE_HEAD(&plugin_op_list, entries);
		(void)dlclose(ope->handle);
		free(ope);
	}
}

/*
 * Find an input plug-in based on the extension of the specified file.
 */
const struct ip *
plugin_find_ip(const char *file)
{
	struct plugin_ip_entry	*ipe;
	int			 i;
	char			*ext;

	if ((ext = strrchr(file, '.')) == NULL || *++ext == '\0')
		return NULL;

	SLIST_FOREACH(ipe, &plugin_ip_list, entries)
		for (i = 0; ipe->ip->extensions[i] != NULL; i++)
			if (!strcasecmp(ext, ipe->ip->extensions[i]))
				return ipe->ip;

	return NULL;
}

/*
 * Find an output plug-in by name or, if the name is "default", by priority.
 */
const struct op *
plugin_find_op(const char *name)
{
	struct plugin_op_entry	*ope;
	const struct op		*op;

	op = NULL;
	if (!strcmp(name, "default")) {
		/* Find plug-in by priority. */
		SLIST_FOREACH(ope, &plugin_op_list, entries)
			if (op == NULL || op->priority > ope->op->priority)
				op = ope->op;
	} else
		/* Find plug-in by name. */
		SLIST_FOREACH(ope, &plugin_op_list, entries)
			if (!strcmp(name, ope->op->name)) {
				op = ope->op;
				break;
			}

	return op;
}

void
plugin_init(void)
{
	plugin_load_dir(PLUGIN_IP_DIR, "ip", plugin_add_ip);
	if (SLIST_EMPTY(&plugin_ip_list)) {
		LOG_ERRX("%s: no input plug-ins found", PLUGIN_IP_DIR);
		msg_errx("No input plug-ins found");
	}

	plugin_load_dir(PLUGIN_OP_DIR, "op", plugin_add_op);
	if (SLIST_EMPTY(&plugin_op_list)) {
		LOG_ERRX("%s: no output plug-ins found", PLUGIN_OP_DIR);
		msg_errx("No output plug-ins found");
	}
}

static void
plugin_load_dir(const char *dir, const char *symbol,
    void (*add)(void *, void *))
{
	struct dir		*d;
	struct dir_entry	*de;
	char			*ext;
	void			*handle, *plugin;

	if ((d = dir_open(dir)) == NULL) {
		msg_err("%s", dir);
		return;
	}

	while ((de = dir_get_entry(d)) != NULL) {
		if (errno) {
			msg_err("%s", de->path);
			continue;
		}

		if (de->type != FILE_TYPE_REGULAR)
			continue;

		if ((ext = strrchr(de->name, '.')) == NULL ||
		    strcmp(ext, ".so"))
			continue;

		if ((handle = dlopen(de->path, RTLD_LAZY | RTLD_LOCAL)) ==
		    NULL) {
			LOG_ERRX("dlopen: %s: %s", de->path, dlerror());
			continue;
		}

		if ((plugin = dlsym(handle, symbol)) == NULL) {
			LOG_ERRX("dlsym: %s: %s", de->path, dlerror());
			(void)dlclose(handle);
			continue;
		}

		add(handle, plugin);
	}

	if (errno)
		msg_err("%s", dir);

	dir_close(d);
}
