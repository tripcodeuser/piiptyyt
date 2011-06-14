
#include <stddef.h>
#include <assert.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

#include "defs.h"
#include "pt-user-info.h"


PtUserInfo *pt_user_info_new(void) {
	return g_object_new(PT_USER_INFO_TYPE, NULL);
}


PtUserInfo *pt_user_info_new_from_json(JsonObject *obj)
{
	PtUserInfo *self = pt_user_info_new();
	/* TODO: call format_from_json() */
	return self;
}


GdkPixbuf *pt_user_info_get_userpic(PtUserInfo *self)
{
	/* TODO: access the userpic cache */
	return NULL;
}


static void pt_user_info_init(PtUserInfo *self)
{
	self->id = 0;
	self->longname = NULL;
	self->screenname = NULL;
	self->profile_image_url = NULL;
	self->cached_img_name = NULL;

	self->cache_parent = NULL;
	self->dirty = false;
}


static void pt_user_info_finalize(GObject *object)
{
	PtUserInfo *self = PT_USER_INFO(object);

	g_free(self->longname);
	g_free(self->screenname);
	g_free(self->profile_image_url);
	g_free(self->cached_img_name);

	GObjectClass *parent_class = g_type_class_peek_parent(
		PT_USER_INFO_GET_CLASS(self));
	parent_class->finalize(object);
}


static void pt_user_info_class_init(PtUserInfoClass *klass)
{
	GObjectClass *obj_class = G_OBJECT_CLASS(klass);
	obj_class->finalize = &pt_user_info_finalize;
}


G_DEFINE_TYPE(PtUserInfo, pt_user_info, G_TYPE_OBJECT);
