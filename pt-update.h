
/* GObject for PtUpdate. */

#ifndef SEEN_PT_UPDATE_H
#define SEEN_PT_UPDATE_H

#include <stdint.h>
#include <glib.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>


#define PT_UPDATE_TYPE (pt_update_get_type())
#define PT_UPDATE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), PT_UPDATE_TYPE, PtUpdate))
#define PT_IS_UPDATE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), PT_UPDATE_TYPE))
#define PT_UPDATE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), PT_UPDATE_TYPE, PtUpdateClass))
#define PT_IS_UPDATE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), PT_UPDATE_TYPE))
#define PT_UPDATE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), PT_UPDATE_TYPE, PtUpdateClass))


typedef struct update PtUpdate;
typedef struct _pt_update_class PtUpdateClass;


/* the almighty "update", also known as a "tweet" or "status".
 *
 * the user info referenced by in_rep_to_uid may not be available in the user
 * cache, and in_rep_to_sid may refer to a status the client has not seen.
 *
 * TODO: this structure should have a "changed" signal for when a missing
 * userpic is loaded, or when such an image changes for some other reason.
 *
 * properties:
 * - "markup" (string, ro)
 */
struct update
{
	GObject parent_instance;

	uint64_t id;			/* status id on the microblog service. */
	bool favorited, truncated;
	uint64_t in_rep_to_uid;	/* 0 when not a reply */
	uint64_t in_rep_to_sid;	/* status id, or -''- */
	char *in_rep_to_screen_name;
	const char *source;		/* "web", "piiptyyt", etc */
	char *text;				/* UTF-8 */
	GDateTime *timestamp;

	/* TODO: user_info should also be a GObject that has an "userpic"
	 * property. this would be picked up by a column function in model.c,
	 * and set by the "add stuff to model" mechanism according to whether the
	 * update is a forward or not.
	 */
	struct user_info *user;

	char *markup_cache;
};


struct _pt_update_class
{
	GObjectClass parent_class;
	/* with g_string_chunk_insert_const(): values for PtUpdate->source. */
	GStringChunk *source_chunk;
};


extern GType pt_update_get_type(void);

extern PtUpdate *pt_update_new(void);
extern PtUpdate *pt_update_new_from_json(
	JsonObject *obj,
	struct user_cache *uc,
	GError **err_p);

/* get the GdkPixbuf representing the avatar picture to be displayed next to
 * this update. for forwarded updates ("retweets"), returns the originator's
 * userpic and not the re-sender's.
 *
 * if the userpic is not (yet) available, returns NULL. return value is owned
 * by caller.
 */
extern GdkPixbuf *pt_update_get_display_user_pic(PtUpdate *update);

/* as above, but always returns a reference to the re-sender's userpic even
 * for forwards.
 */
extern GdkPixbuf *pt_update_get_sender_user_pic(PtUpdate *update);

#endif
