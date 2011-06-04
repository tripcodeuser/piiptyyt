
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include "defs.h"


bool format_from_json(
	void *dest,
	JsonObject *obj,
	const struct field_desc *fields,
	size_t num_fields)
{
	bool changed = false;

	for(size_t i=0; i < num_fields; i++) {
		const char *name = fields[i].name;
		if(json_object_get_member(obj, name) == NULL) {
			/* not present. skip. */
			continue;
		}
		void *ptr = dest + fields[i].offset;
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

	return changed;
}
