
#ifndef SEEN_PT_USER_INFO_H
#define SEEN_PT_USER_INFO_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>


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
	struct user_cache *cache_parent;
	bool dirty;		/* sync to database on destroy? */
};


struct _pt_user_info_class
{
	GObjectClass parent_class;
};


/* returns a borrowed GdkPixbuf reference, or NULL when it's not available. */
extern GdkPixbuf *pt_user_info_get_userpic(struct user_info *info);

/* for usercache.c, and for dummy objects possibly */

extern PtUserInfo *pt_user_info_new(void);
extern PtUserInfo *pt_user_info_new_from_json(JsonObject *obj);

extern GType pt_user_info_get_type(void);

#endif
