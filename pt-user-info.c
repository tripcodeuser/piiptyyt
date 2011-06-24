
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include "defs.h"
#include "pt-user-info.h"


enum prop_names {
	PROP_USERPIC = 1,
	PROP__LAST
};


static GParamSpec *properties[PROP__LAST];


PtUserInfo *pt_user_info_new(void) {
	return g_object_new(PT_USER_INFO_TYPE, NULL);
}


bool pt_user_info_from_json(PtUserInfo *ui, JsonObject *obj, GError **err_p)
{
	int num_fields = 0;
	const struct field_desc *fields = pt_user_info_get_field_desc(
		&num_fields);
	return format_from_json(ui, obj, fields, num_fields, err_p);
}


static char *cached_userpic_name(PtUserInfo *self)
{
	const char *ext = strrchr(self->profile_image_url, '.');
	if(ext == NULL || ext[0] == '\0') ext = ".dat";
	return g_strdup_printf("u_%llu.%s", (unsigned long long)self->id,
		&ext[1]);
}


static char *cached_userpic_path(const char *name)
{
	return g_build_filename(g_get_user_cache_dir(),
		"piiptyyt", "userpic", name, NULL);
}


static void img_fetch_callback(
	SoupSession *session,
	SoupMessage *msg,
	gpointer dataptr)
{
	PtUserInfo *self = PT_USER_INFO(dataptr);
	g_return_if_fail(msg == self->img_fetch_msg);

	if(msg->status_code == SOUP_STATUS_OK) {
		/* store response data associated with the user id. */
		char *cached_name = cached_userpic_name(self),
			*cache_path = cached_userpic_path(cached_name),
			*write_path = g_strconcat(cache_path, ".tmp", NULL);
		GError *err = NULL;
		if(!g_file_set_contents(write_path, msg->response_body->data,
			msg->response_body->length, &err))
		{
			g_warning("unable to store to `%s': %s", write_path,
				err->message);
			g_error_free(err);
		} else {
			int n = rename(write_path, cache_path);
			if(n == 1) {
				g_warning("can't move `%s' to `%s': %s", write_path,
					cache_path, strerror(errno));
			}
		}
		g_free(write_path);
		g_free(cache_path);
		g_free(self->cached_img_name);
		self->cached_img_name = cached_name;
		self->dirty = true;

		g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_USERPIC]);

		/* TODO: store expiry, refresh userimages with appropriate headers for
		 * "not changed" etc
		 */

		printf("userpic for `%s' retrieved (%zd bytes)\n", self->screenname,
			(ssize_t)msg->response_body->length);
		const char *expires = soup_message_headers_get_one(
			msg->response_headers, "Expires");
		printf("... expires on `%s'.\n",
			expires != NULL ? expires : "(unknown)");
	} else {
		g_debug("can't retrieve url `%s' (userpic for uid %llu, `%s')",
			self->profile_image_url, (unsigned long long)self->id,
			self->screenname);
	}

	self->img_fetch_msg = NULL;
	/* caller unrefs `msg'. */
}


static void start_userpic_fetch(PtUserInfo *self, SoupSession *session)
{
	if(self->img_fetch_msg != NULL) {
		/* TODO: check for timeout? or something. */
		return;
	}

	self->img_fetch_msg = soup_message_new("GET", self->profile_image_url);
	soup_session_queue_message(session, self->img_fetch_msg,
		&img_fetch_callback, self);
}


GdkPixbuf *pt_user_info_get_userpic(PtUserInfo *self, SoupSession *session)
{
	if(self->cached_img_name == NULL) {
		if(session != NULL) start_userpic_fetch(self, session);
		return NULL;
	} else {
		GdkPixbuf *ret;
		GObject *obj = pt_cache_get(self->userpic_cache,
			self->cached_img_name);
		if(obj != NULL) {
			printf("userpic hit for `%s'\n", self->cached_img_name);
			ret = GDK_PIXBUF(g_object_ref(obj));	/* retain */
		} else {
			printf("userpic miss for `%s'\n", self->cached_img_name);
			char *filename = cached_userpic_path(self->cached_img_name);
			GError *err = NULL;
			ret = gdk_pixbuf_new_from_file(filename, &err);
			if(ret == NULL) {
				/* TODO: if this is ENOENT, clear the cached field and set
				 * dirty.
				 */
				g_warning("can't read userpic `%s': %s", filename,
					err->message);
				g_error_free(err);
			} else {
				pt_cache_put(self->userpic_cache, self->cached_img_name,
					strlen(self->cached_img_name) + 1, G_OBJECT(ret));
			}
			g_free(filename);
		}

		return ret;
	}
}


const struct field_desc *pt_user_info_get_field_desc(int *count_p)
{
	static const struct field_desc fields[] = {
		SQL_FIELD(struct user_info, 's', longname, "name", "longname"),
		SQL_FIELD(struct user_info, 's', screenname, "screen_name", "screenname"),
		FLD(struct user_info, 's', profile_image_url),
		FLD(struct user_info, 'b', protected),
		FLD(struct user_info, 'b', verified),
		FLD(struct user_info, 'b', following),
		FLD(struct user_info, 'I', id),
	};

	*count_p = G_N_ELEMENTS(fields);
	return fields;
}


static void pt_user_info_get_property(
	GObject *object,
	guint prop_id,
	GValue *value,
	GParamSpec *spec)
{
	PtUserInfo *self = PT_USER_INFO(object);
	switch(prop_id) {
	case PROP_USERPIC:
		g_value_take_object(value, pt_user_info_get_userpic(self, NULL));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, spec);
		break;
	}
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
	self->img_fetch_msg = NULL;

	PtUserInfoClass *klass = PT_USER_INFO_GET_CLASS(self);
	if(klass->userpic_cache != NULL) {
		/* use the existing cache. */
		GObject *obj = g_object_ref(klass->userpic_cache);
		assert(obj != NULL);
		self->userpic_cache = PT_CACHE(obj);
		assert(self->userpic_cache != NULL);
	} else {
		/* make a new cache instance and hang it off a weak reference. */
		GObject *obj = g_object_new(PT_CACHE_TYPE,
			"hash-fn", &g_str_hash, "equal-fn", &g_str_equal,
			"high-watermark", 120, "low-watermark", 80,
			/* no flush function. this cache is read-only. */
			NULL);
		self->userpic_cache = PT_CACHE(obj);	/* keep ref */
		klass->userpic_cache = obj;
		g_object_add_weak_pointer(obj, &klass->userpic_cache);
	}
}


static void pt_user_info_dispose(GObject *object)
{
	PtUserInfo *self = PT_USER_INFO(object);

	if(self->userpic_cache != NULL) {
		g_object_unref(self->userpic_cache);
		self->userpic_cache = NULL;
	}
	if(self->img_fetch_msg != NULL) {
		g_object_unref(self->img_fetch_msg);
		self->img_fetch_msg = NULL;
	}

	GObjectClass *parent_class = g_type_class_peek_parent(
		PT_USER_INFO_GET_CLASS(self));
	parent_class->dispose(object);
}


static void pt_user_info_finalize(GObject *object)
{
	PtUserInfo *self = PT_USER_INFO(object);

	g_free(self->longname); self->longname = NULL;
	g_free(self->screenname); self->screenname = NULL;
	g_free(self->profile_image_url); self->profile_image_url = NULL;
	g_free(self->cached_img_name); self->cached_img_name = NULL;

	GObjectClass *parent_class = g_type_class_peek_parent(
		PT_USER_INFO_GET_CLASS(self));
	parent_class->finalize(object);
}


static void pt_user_info_class_init(PtUserInfoClass *klass)
{
	klass->userpic_cache = NULL;

	GObjectClass *obj_class = G_OBJECT_CLASS(klass);
	obj_class->finalize = &pt_user_info_finalize;
	obj_class->dispose = &pt_user_info_dispose;
	obj_class->get_property = &pt_user_info_get_property;

	properties[0] = NULL;	/* for the homies. */
	properties[PROP_USERPIC] = g_param_spec_object("userpic",
		"User picture (\"avatar\") connected to this user info",
		"Get reference to user image as GdkPixbuf",
		GDK_TYPE_PIXBUF,
		G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	for(int i=1; i < PROP__LAST; i++) {
		g_object_class_install_property(obj_class, i, properties[i]);
	}
}


G_DEFINE_TYPE(PtUserInfo, pt_user_info, G_TYPE_OBJECT);
