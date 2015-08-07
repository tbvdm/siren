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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "siren.h"

struct format {
	size_t			 fixedwidth;
	char			*formatstr;
	struct format_part	*part;
};

struct format_field {
	char			*name;
	enum {
		FORMAT_ALIGN_LEFT,
		FORMAT_ALIGN_RIGHT
	} align;
	char			 padchar;
	int			 width;
	int			 conditional;
	char			*trueval;
	char			*falseval;
};

struct format_literal {
	char			*string;
	size_t			 len;
};

struct format_part {
	enum {
		FORMAT_PART_FIELD,
		FORMAT_PART_LITERAL
	} type;
	union {
		struct format_field	 field;
		struct format_literal	 literal;
	} data;
	struct format_part	*next;
};

static size_t	format_write_literal(char *, size_t, size_t, const char *,
		    size_t);
static size_t	format_write_field(char *, size_t, size_t, const char *,
		    const struct format_field *, size_t);

void
format_free(struct format *f)
{
	struct format_part *p, *np;

	if (f == NULL)
		return;

	for (p = f->part; p != NULL; p = np) {
		switch (p->type) {
		case FORMAT_PART_LITERAL:
			free(p->data.literal.string);
			break;
		case FORMAT_PART_FIELD:
			free(p->data.field.name);
			free(p->data.field.trueval);
			free(p->data.field.falseval);
			break;
		}
		np = p->next;
		free(p);
	}

	free(f->formatstr);
	free(f);
}

static int
format_get_field(const char **fmt, struct format_field *fld)
{
	size_t	 len;
	char	*otmp, *tmp;

	fld->name = NULL;
	fld->align = FORMAT_ALIGN_RIGHT;
	fld->padchar = ' ';
	fld->width = 0;
	fld->conditional = 0;
	fld->trueval = NULL;
	fld->falseval = NULL;

	/* Handle alignment. */
	if (**fmt == '-') {
		fld->align = FORMAT_ALIGN_LEFT;
		(*fmt)++;
	}

	/* Handle padding character. */
	if (**fmt == '0') {
		/* Allow zero-padding only when right-aligning. */
		if (fld->align == FORMAT_ALIGN_RIGHT)
			fld->padchar = '0';
		(*fmt)++;
	}

	/* Handle field width. */
	if (**fmt == '*') {
		/* Variable width. */
		fld->width = -1;
		(*fmt)++;
	} else
		/* Explicit fixed width. */
		while (isdigit((unsigned char)**fmt)) {
			fld->width = 10 * fld->width + (**fmt - '0');
			(*fmt)++;
		}

	/* Check if variable name is missing. */
	if (**fmt == '\0')
		return -1;

	/*
	 * Check if this is a single-character name (e.g. "%f") or a
	 * multi-character one (e.g. "%{foo}").
	 */
	if (**fmt != '{') {
		fld->name = xstrndup(*fmt, 1);
		(*fmt)++;
	} else {
		(*fmt)++;
		len = strcspn(*fmt, "}");

		/* Check if closing brace is missing. */
		if ((*fmt)[len] == '\0') {
			*fmt += len;
			return -1;
		}

		/* Check for empty specifier ("%{}"). */
		if (len == 0) {
			(*fmt)++;
			return -1;
		}

		/*
		 * Check if this is conditional field (e.g. "%{?foo,yes,no}").
		 */
		if (**fmt != '?') {
			fld->name = xstrndup(*fmt, len);
			*fmt += len + 1;
		} else {
			(*fmt)++;
			fld->conditional = 1;
			tmp = otmp = xstrndup(*fmt, len - 1);
			*fmt += len;

			/* Copy variable name. */
			len = strcspn(tmp, ",");
			if (len == 0)
				return -1;
			fld->name = xstrndup(tmp, len);

			/* Copy value-if-condition-is-true. */
			tmp += len;
			if (*tmp != '\0')
				tmp++;
			len = strcspn(tmp, ",");
			fld->trueval = xstrndup(tmp, len);

			/* Copy value-if-condition-is-false. */
			tmp += len;
			if (*tmp != '\0')
				tmp++;
			fld->falseval = xstrdup(tmp);

			free(otmp);
		}
	}

	return 0;
}

static const char *
format_get_value(char *buf, size_t bufsize, const struct format_field *fld,
    const struct format_variable *vars, size_t nvars)
{
	size_t		 i;
	int		 condition;
	const char	*value;

	/* Find variable. */
	if (fld->name[0] != '\0' && fld->name[1] == '\0') {
		for (i = 0; i < nvars; i++)
			if (*fld->name == vars[i].sname)
				break;
	} else
		for (i = 0; i < nvars; i++)
			if (!strcmp(fld->name, vars[i].lname))
				break;

	/* Check if there was a match. */
	if (i == nvars)
		return NULL;

	/* Silence gcc. */
	condition = 0;

	switch (vars[i].type) {
	case FORMAT_VARIABLE_NUMBER:
		if (fld->conditional)
			condition = (vars[i].value.number != 0);
		else {
			xsnprintf(buf, bufsize, "%d", vars[i].value.number);
			value = buf;
		}
		break;
	case FORMAT_VARIABLE_STRING:
		if (fld->conditional)
			condition = (vars[i].value.string[0] != '\0');
		else
			value = vars[i].value.string;
		break;
	case FORMAT_VARIABLE_TIME:
		if (fld->conditional)
			condition = (vars[i].value.time != 0);
		else {
			if (vars[i].value.time >= 3600)
				xsnprintf(buf, bufsize, "%u:%02u:%02u",
				    HOURS(vars[i].value.time),
				    HMINS(vars[i].value.time),
				    MSECS(vars[i].value.time));
			else
				xsnprintf(buf, bufsize, "%u:%02u",
				    MINS(vars[i].value.time),
				    MSECS(vars[i].value.time));

			value = buf;
		}
		break;
	default:
		value = NULL;
		break;
	}

	if (fld->conditional)
		value = condition ? fld->trueval : fld->falseval;

	return value;
}

struct format *
format_parse(const char *fmt)
{
	struct format		 *f;
	struct format_part	**p;
	size_t			  len;

	f = xmalloc(sizeof *f);
	f->fixedwidth = 0;
	f->formatstr = xstrdup(fmt);
	p = &f->part;

	/*
	 * Parse the format string and break it up in "literal" parts and
	 * "field" parts.
	 */
	while (*fmt != '\0') {
		*p = xmalloc(sizeof **p);

		if (*fmt != '%' || *++fmt == '%') {
			/* Literal part. */
			(*p)->type = FORMAT_PART_LITERAL;

			if (*fmt == '%')
				len = 1;
			else
				len = strcspn(fmt, "%");

			(*p)->data.literal.string = xstrndup(fmt, len);
			(*p)->data.literal.len = len;
			f->fixedwidth += len;
			fmt += len;
		} else {
			/* Field part. */
			(*p)->type = FORMAT_PART_FIELD;

			if (format_get_field(&fmt, &(*p)->data.field) == -1) {
				/* Skip invalid field. */
				free(*p);
				continue;
			}

			if ((*p)->data.field.width != -1)
				f->fixedwidth += (*p)->data.field.width;
		}

		p = &(*p)->next;
	}

	*p = NULL;

	return f;
}

void
format_snprintf(char *buf, size_t bufsize, const struct format *f,
    const struct format_variable *vars, size_t nvars)
{
	struct format_part	*part;
	size_t			 off, nvarwidthfields, valuelen, varwidth,
				 width;
	char			 tmp[500];
	const char		*value;

	if (bufsize == 0)
		return;

	/*
	 * Determine the number of variable-width fields and the amount of
	 * space available to them.
	 */

	if (bufsize - 1 < f->fixedwidth)
		varwidth = 0;
	else
		varwidth = bufsize - f->fixedwidth - 1;

	nvarwidthfields = 0;
	for (part = f->part; part != NULL; part = part->next) {
		if (part->type != FORMAT_PART_FIELD)
			/* This is not a field part. */
			continue;

		if (part->data.field.width == -1) {
			/* This is a variable-width field. */
			nvarwidthfields++;
			continue;
		}

		if (part->data.field.width != 0)
			/* This field has an explicit fixed width. */
			continue;

		/* Get the field value to determine its length. */
		value = format_get_value(tmp, sizeof tmp, &part->data.field,
		    vars, nvars);
		if (value != NULL) {
			valuelen = strlen(value);
			if (varwidth < valuelen)
				varwidth = 0;
			else
				varwidth -= valuelen;
		}
	}

	/*
	 * Construct the formatted string.
	 */

	off = 0;
	for (part = f->part; part != NULL; part = part->next)
		switch (part->type) {
		case FORMAT_PART_LITERAL:
			off += format_write_literal(buf, off, bufsize,
			    part->data.literal.string,
			    part->data.literal.len);
			break;
		case FORMAT_PART_FIELD:
			value = format_get_value(tmp, sizeof tmp,
			    &part->data.field, vars, nvars);

			/* Silence gcc. */
			width = 0;

			/*
			 * If this a variable-width field, then calculate the
			 * space available to it.
			 */
			if (part->data.field.width == -1) {
				width = (varwidth + nvarwidthfields - 1) /
				    nvarwidthfields;
				varwidth -= width;
				nvarwidthfields--;
			}

			off += format_write_field(buf, off, bufsize, value,
			    &part->data.field, width);
			break;
		}

	buf[off] = '\0';
}

const char *
format_to_string(const struct format *f)
{
	return f->formatstr;
}

void
format_track_snprintf(char *buf, size_t bufsize, const struct format *f,
    const struct track *t)
{
	struct format_variable vars[10];

	vars[0].lname = "albumartist";
	vars[0].sname = 'A';
	vars[0].type = FORMAT_VARIABLE_STRING;
	vars[0].value.string = t->albumartist ? t->albumartist : "";
	vars[1].lname = "artist";
	vars[1].sname = 'a';
	vars[1].type = FORMAT_VARIABLE_STRING;
	vars[1].value.string = t->artist ? t->artist : "";
	vars[2].lname = "discnumber";
	vars[2].sname = 'c';
	vars[2].type = FORMAT_VARIABLE_STRING;
	vars[2].value.string = t->discnumber ? t->discnumber : "";
	vars[3].lname = "duration";
	vars[3].sname = 'd';
	vars[3].type = FORMAT_VARIABLE_TIME;
	vars[3].value.time = t->duration;
	vars[4].lname = "path";
	vars[4].sname = 'f';
	vars[4].type = FORMAT_VARIABLE_STRING;
	vars[4].value.string = t->path;
	vars[5].lname = "genre";
	vars[5].sname = 'g';
	vars[5].type = FORMAT_VARIABLE_STRING;
	vars[5].value.string = t->genre ? t->genre : "";
	vars[6].lname = "album";
	vars[6].sname = 'l';
	vars[6].type = FORMAT_VARIABLE_STRING;
	vars[6].value.string = t->album ? t->album : "";
	vars[7].lname = "tracknumber";
	vars[7].sname = 'n';
	vars[7].type = FORMAT_VARIABLE_STRING;
	vars[7].value.string = t->tracknumber ? t->tracknumber : "";
	vars[8].lname = "title";
	vars[8].sname = 't';
	vars[8].type = FORMAT_VARIABLE_STRING;
	vars[8].value.string = t->title ? t->title : "";
	vars[9].lname = "date";
	vars[9].sname = 'y';
	vars[9].type = FORMAT_VARIABLE_STRING;
	vars[9].value.string = t->date ? t->date : "";

	format_snprintf(buf, bufsize, f, vars, NELEMENTS(vars));
}

static size_t
format_write_literal(char *buf, size_t off, size_t bufsize, const char *str,
    size_t len)
{
	if (off >= bufsize)
		return 0;

	if (len > bufsize - off - 1)
		len = bufsize - off - 1;

	memcpy(buf + off, str, len);
	return len;
}

static size_t
format_write_field(char *buf, size_t off, size_t bufsize, const char *value,
    const struct format_field *fld, size_t varwidth)
{
	size_t padlen, valuelen, width;

	if (off >= bufsize)
		return 0;

	if (value == NULL)
		valuelen = 0;
	else
		valuelen = strlen(value);

	if (fld->width == -1) {
		/* Variable-width field specified. */
		width = varwidth;
		if (valuelen > width)
			valuelen = width;
	} else if (fld->width == 0) {
		/* No width specified; use value length. */
		if (valuelen > bufsize - off - 1)
			valuelen = bufsize - off - 1;
		width = valuelen;
	} else {
		/* Fixed width specified. */
		if ((unsigned int)fld->width < bufsize - off - 1)
			width = fld->width;
		else
			width = bufsize - off - 1;
		if (valuelen > width)
			valuelen = width;
	}

	padlen = width - valuelen;

	if (fld->align == FORMAT_ALIGN_RIGHT) {
		memset(buf + off, fld->padchar, padlen);
		off += padlen;
	}
	if (value != NULL) {
		memcpy(buf + off, value, valuelen);
		off += valuelen;
	}
	if (fld->align == FORMAT_ALIGN_LEFT)
		memset(buf + off, fld->padchar, padlen);

	return width;
}
