
#include <stddef.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include "defs.h"


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


#define UF(type, fieldname, name) { (name), offsetof(struct update, fieldname), (type) }
#define UFS(type, name) UF(type, name, #name)

struct update *update_new_from_json(JsonObject *obj, GError **err_p)
{
	static const struct {
		const char *name;
		off_t offset;
		char type;
	} fields[] = {
		UFS('i', id),
		UF('i', in_rep_to_uid, "in_reply_to_user_id"),
		UF('i', in_rep_to_sid, "in_reply_to_status_id"),
		UFS('b', favorited),
		UFS('b', truncated),
		UF('s', in_rep_to_screen_name, "in_reply_to_screen_name"),
		UFS('s', source),
		UFS('s', text),
	};

	struct update *u = update_new();
	for(int i=0; i < G_N_ELEMENTS(fields); i++) {
		const char *name = fields[i].name;
		if(json_object_get_member(obj, name) == NULL) {
			/* not present. skip. */
			continue;
		}
		void *ptr = (void *)u + fields[i].offset;
		switch(fields[i].type) {
		case 'i':
			*(uint64_t *)ptr = json_object_get_int_member(obj, name);
			break;
		case 'b':
			*(bool *)ptr = json_object_get_boolean_member(obj, name) != FALSE;
			break;
		case 's':
			*(char **)ptr = g_strdup(json_object_get_string_member(obj, name));
			break;
		}
	}

	return u;
}
