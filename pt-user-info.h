
#ifndef SEEN_PT_USER_INFO_H
#define SEEN_PT_USER_INFO_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include "defs.h"
#include "pt-cache.h"


#define PT_USER_INFO_TYPE (pt_user_info_get_type())
#define PT_USER_INFO(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), PT_USER_INFO_TYPE, PtUserInfo))
#define PT_IS_USER_INFO(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), PT_USER_INFO_TYPE))
#define PT_USER_INFO_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), PT_USER_INFO_TYPE, PtUserInfoClass))
#define PT_IS_USER_INFO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), PT_USER_INFO_TYPE))
#define PT_USER_INFO_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), PT_USER_INFO_TYPE, PtUserInfoClass))


typedef struct user_info PtUserInfo;
typedef struct _pt_user_info_class PtUserInfoClass;


/* an empty user_info occurs when the client has only seen a trimmed user JSON
 * object. those can be recognized by ->screenname == NULL.
 *
 * properties:
 * - "userpic" (GdkPixbuf *, ro). reading is equivalent to
 *   pt_user_info_get_userpic(obj, NULL) .
 */
struct user_info
{
	GObject parent_instance;

	uint64_t id;
	char *longname, *screenname;
	char *profile_image_url;
	bool protected, verified, following;

	/* not from JSON, but in database */
	char *cached_img_name;
	time_t cached_img_expires;

	/* non-database, non-json fields */
	bool dirty;		/* sync to database on destroy? */
	SoupMessage *img_fetch_msg;
	PtCache *userpic_cache;		/* ref */
};


struct _pt_user_info_class
{
	GObjectClass parent_class;
	gpointer userpic_cache;		/* GObject * (really PtCache *), weak ref */
};


/* returns a GdkPixbuf reference, or NULL when the image isn't available.
 *
 * if `session' is not NULL, and the image is not cached locally, starts a
 * HTTP transaction with a server using the given session. the transaction's
 * completion will be notified to "notify::userpic". it's likely good practice
 * to double-check the result after connecting that signal after a "in
 * progress" outcome.
 */
extern GdkPixbuf *pt_user_info_get_userpic(
	PtUserInfo *info,
	SoupSession *session);

extern const struct field_desc *pt_user_info_get_field_desc(int *count_p);

/* for usercache.c, and for dummy objects possibly */
extern PtUserInfo *pt_user_info_new(void);

/* returns `false', errors on failure. */
extern bool pt_user_info_from_json(
	PtUserInfo *ui,
	JsonObject *obj,
	GError **err_p);


extern GType pt_user_info_get_type(void);

#endif
