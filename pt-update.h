
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
 * this structure is a candidate for making into a GObject, for things such as
 * early display of updates that're in reply to uids or sids that the client
 * doesn't remember; a signal would be popped once the contents have settled
 * due to a http transaction completing or data coming down the wire.
 */
struct update
{
	GObject parent_instance;

	uint64_t id;			/* status id on the microblog service. */
	bool favorited, truncated;
	uint64_t in_rep_to_uid;	/* 0 when not a reply */
	uint64_t in_rep_to_sid;	/* status id, or -''- */
	char *in_rep_to_screen_name;
	char *source;	/* "web", "piiptyyt", etc (should be a local source id) */
	char *text;				/* UTF-8 */

	struct user_info *user;
};


struct _pt_update_class
{
	GObjectClass parent_class;
};


extern GType pt_update_get_type(void);

extern PtUpdate *pt_update_new(void);
extern PtUpdate *pt_update_new_from_json(
	JsonObject *obj,
	struct user_cache *uc,
	GError **err_p);


#endif
