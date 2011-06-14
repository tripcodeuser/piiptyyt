
#include <stddef.h>
#include <assert.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

#include "defs.h"
#include "pt-update.h"


enum prop_names {
	PROP_MARKUP = 1,
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
};


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

	assert(u != NULL || err_p == NULL || *err_p != NULL);
	return u;
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
		/* TODO */
		g_value_set_string(value, self->text);
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
}


static void pt_update_dispose(GObject *object)
{
	PtUpdate *u = PT_UPDATE(object);
	if(u == NULL) return;

	if(u->user != NULL) {
		user_info_put(u->user);
		u->user = NULL;
	}

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
	g_free(u->source);
	g_free(u->text);

	GObjectClass *parent_class = g_type_class_peek_parent(
		PT_UPDATE_GET_CLASS(u));
	parent_class->finalize(object);
}


static void pt_update_class_init(PtUpdateClass *klass)
{
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
