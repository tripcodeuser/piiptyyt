
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

#include "defs.h"
#include "pt-update.h"
#include "pt-user-info.h"


enum prop_names {
	PROP_MARKUP = 1,
};


struct source_uri_data
{
	char **uri;
	GString *text;
};


#define UF(t, f, n) FIELD(struct update, t, f, n)
#define UFS(t, n) FIELD(struct update, t, n, #n)

static const struct field_desc update_fields[] = {
	UFS('I', id),
	UF('i', in_rep_to_uid, "in_reply_to_user_id"),
	UF('i', in_rep_to_sid, "in_reply_to_status_id"),
	UFS('b', favorited),
	UFS('b', truncated),
	UF('s', in_rep_to_screen_name, "in_reply_to_screen_name"),
	UFS('s', source),
	UFS('S', text),
	UF('T', timestamp, "created_at"),
};


static void source_uri_element(
	GMarkupParseContext *ctx,
	const char *element,
	const char **attr_names,
	const char **attr_vals,
	gpointer dataptr,
	GError **err_p)
{
	struct source_uri_data *dat = dataptr;

	if(strcmp(element, "a") != 0) {
		g_debug("saw element `%s', which is not understood in \"source\"",
			element);
		return;
	}

	const char *rel = NULL, *href = NULL;
	for(int i=0; attr_names[i] != NULL; i++) {
		if(strcmp(attr_names[i], "rel") == 0) rel = attr_vals[i];
		else if(strcmp(attr_names[i], "href") == 0) href = attr_vals[i];
		else {
			g_debug("unknown attribute for `a': `%s'", attr_names[i]);
		}
	}
	if(strcmp(rel, "nofollow") != 0) {
		g_debug("unknown value for `a#rel': `%s'", rel);
	}
	g_free(*dat->uri);
	*dat->uri = g_strdup(href);
}


static void source_uri_text(
	GMarkupParseContext *ctx,
	const char *text,
	gsize text_len,
	gpointer dataptr,
	GError **err_p)
{
	struct source_uri_data *dat = dataptr;
	g_string_append_len(dat->text, text, text_len);
}


static bool separate_source_uri(
	char **source_uri,
	char **source_text,
	const char *source,
	GError **err_p)
{
	static const GMarkupParser parser = {
		.start_element = &source_uri_element,
		.text = &source_uri_text,
	};
	struct source_uri_data dat = {
		.uri = source_uri,
		.text = g_string_sized_new(128),
	};
	GMarkupParseContext *ctx = g_markup_parse_context_new(&parser,
		G_MARKUP_TREAT_CDATA_AS_TEXT | G_MARKUP_PREFIX_ERROR_POSITION,
		&dat, NULL);

	gboolean ok = g_markup_parse_context_parse(ctx, source, strlen(source),
		err_p);
	if(ok) {
		ok = g_markup_parse_context_end_parse(ctx, err_p);
	}

	g_markup_parse_context_free(ctx);
	if(ok) {
		*source_text = g_string_free(dat.text, FALSE);
		return true;
	} else {
		g_string_free(dat.text, TRUE);
		return false;
	}
}


PtUpdate *pt_update_new(void) {
	return PT_UPDATE(g_object_new(PT_UPDATE_TYPE, NULL));
}


PtUpdate *pt_update_new_from_json(
	JsonObject *obj,
	struct user_cache *uc,
	GError **err_p)
{
	PtUpdate *u = pt_update_new();
	if(!format_from_json(u, obj,
		update_fields, G_N_ELEMENTS(update_fields), err_p))
	{
		g_object_unref(G_OBJECT(u));
		u = NULL;
	} else if(uc != NULL && !json_object_get_null_member(obj, "user")) {
		JsonObject *user = json_object_get_object_member(obj, "user");
		if(user != NULL) u->user = user_info_get_from_json(uc, user);
	}

	char *new_src = NULL;
	if(strchr(u->source, '<') != NULL) {
		/* the "source" string may be in XML. separate the URI and content. */
		char *source_uri = NULL;
		GError *err = NULL;
		if(separate_source_uri(&source_uri, &new_src, u->source, &err)) {
			g_free(source_uri);		/* not kept. */
		} else {
			g_debug("failed to parse source `%s': %s", u->source,
				err->message);
			g_error_free(err);
		}
	}
	if(new_src == NULL) new_src = g_strdup(u->source);
	/* special: format_from_json() insists on duplicating strings into the
	 * structure, but u->source is declared pointer to const. this is the sane
	 * thing to do.
	 */
	g_free((void *)u->source);
	PtUpdateClass *klass = PT_UPDATE_GET_CLASS(u);
	u->source = g_string_chunk_insert_const(klass->source_chunk, new_src);
	g_free(new_src);

	assert(u != NULL || err_p == NULL || *err_p != NULL);
	return u;
}


static char *pt_update_generate_markup(PtUpdate *self)
{
	const char *username;
	if(self->user == NULL || self->user->screenname == NULL) {
		username = "[tanasinn]";
	} else {
		username = self->user->screenname;
	}

	GDateTime *local = g_date_time_to_local(self->timestamp);
	char *time_sent = g_date_time_format(local, "%F %H:%M");
	char *ret = g_markup_printf_escaped(
		"<b>%s</b> %s\n"
		"<span size=\"smaller\" fgcolor=\"grey\">%s from %s</span>",
		username, self->text, time_sent, self->source);
	g_free(time_sent);
	g_date_time_unref(local);
	return ret;
}


static void pt_update_get_property(
	GObject *object,
	guint prop_id,
	GValue *value,
	GParamSpec *spec)
{
	PtUpdate *self = PT_UPDATE(object);
	switch(prop_id) {
	case PROP_MARKUP:
		if(self->markup_cache == NULL) {
			self->markup_cache = pt_update_generate_markup(self);
		}
		g_value_set_string(value, self->markup_cache);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, spec);
		break;
	}
}


static void pt_update_init(PtUpdate *self)
{
	self->id = 0;
	self->in_rep_to_screen_name = NULL;
	self->source = NULL;
	self->text = NULL;
	self->user = NULL;
	self->markup_cache = NULL;
}


static void pt_update_dispose(GObject *object)
{
	PtUpdate *u = PT_UPDATE(object);
	if(u == NULL) return;

	if(u->user != NULL) {
		user_info_put(u->user);
		u->user = NULL;
	}
	g_date_time_unref(u->timestamp);

	GObjectClass *parent_class = g_type_class_peek_parent(
		PT_UPDATE_GET_CLASS(u));
	parent_class->dispose(object);
}


static void pt_update_finalize(GObject *object)
{
	PtUpdate *u = PT_UPDATE(object);
	if(u == NULL) return;

	/* TODO: use a format_free() call to drop the strings? */
	g_free(u->in_rep_to_screen_name);
	g_free(u->text);
	g_free(u->markup_cache);

	GObjectClass *parent_class = g_type_class_peek_parent(
		PT_UPDATE_GET_CLASS(u));
	parent_class->finalize(object);
}


static void pt_update_class_init(PtUpdateClass *klass)
{
	klass->source_chunk = g_string_chunk_new(512);

	GObjectClass *obj_class = G_OBJECT_CLASS(klass);

	obj_class->finalize = &pt_update_finalize;
	obj_class->dispose = &pt_update_dispose;
	obj_class->get_property = &pt_update_get_property;

	g_object_class_install_property(obj_class, PROP_MARKUP,
		g_param_spec_string("markup",
			"Pango markup for update's content",
			"Get markup text",
			"",
			G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}


G_DEFINE_TYPE(PtUpdate, pt_update, G_TYPE_OBJECT);
