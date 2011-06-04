
#include <stddef.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include "defs.h"


#define UF(t, f, n) FIELD(struct update, t, f, n)
#define UFS(t, n) FIELD(struct update, t, n, #n)

static const struct field_desc update_fields[] = {
	UFS('i', id),
	UF('i', in_rep_to_uid, "in_reply_to_user_id"),
	UF('i', in_rep_to_sid, "in_reply_to_status_id"),
	UFS('b', favorited),
	UFS('b', truncated),
	UF('s', in_rep_to_screen_name, "in_reply_to_screen_name"),
	UFS('s', source),
	UFS('s', text),
};


struct update *update_new(void)
{
	return g_new0(struct update, 1);
}


void update_free(struct update *u)
{
	if(u == NULL) return;

	g_free(u->in_rep_to_screen_name);
	g_free(u->source);
	g_free(u->text);
	g_free(u);
}


struct update *update_new_from_json(JsonObject *obj, GError **err_p)
{
	struct update *u = update_new();
	format_from_json(u, obj, update_fields, G_N_ELEMENTS(update_fields));

	return u;
}
