
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <sqlite3.h>

#include "defs.h"


/* FIXME: hunt for changes */
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
			g_free(*(char **)ptr);
			*(char **)ptr = g_strdup(json_object_get_string_member(obj, name));
			break;
		default:
			assert(false);
		}
	}

	return changed;
}


/* TODO: catch and report errors */
void format_to_sqlite(
	sqlite3_stmt *dest,
	const void *src,
	const struct field_desc *fields,
	size_t num_fields)
{
	for(size_t i=0; i < num_fields; i++) {
		const void *ptr = src + fields[i].offset;
		int ix = i + 1;
		switch(fields[i].type) {
		case 'i':
			sqlite3_bind_int64(dest, ix, *(const int64_t *)ptr);
			break;
		case 'b':
			sqlite3_bind_int(dest, ix, *(const bool *)ptr ? 1 : 0);
			break;
		case 's': {
			const char *str = *(char *const *)ptr;
			sqlite3_bind_text(dest, ix, str, strlen(str), SQLITE_TRANSIENT);
			break;
		}
		default:
			assert(false);
		}
	}
}


void format_from_sqlite(
	void *dest,
	sqlite3_stmt *src,
	const struct field_desc *fields,
	size_t num_fields)
{
	for(size_t i=0; i < num_fields; i++) {
		void *ptr = dest + fields[i].offset;
		int ix = i + 1;
		switch(fields[i].type) {
		case 'i':
			*(int64_t *)ptr = sqlite3_column_int64(src, ix);
			break;
		case 'b':
			*(bool *)ptr = sqlite3_column_int(src, ix) != 0;
			break;
		case 's': {
			const void *col = sqlite3_column_text(src, ix);
			char *str = NULL;
			if(col != NULL) {
				int len = sqlite3_column_bytes(src, ix);
				str = g_malloc(len + 1);
				memcpy(str, sqlite3_column_text(src, ix), len);
				str[len] = '\0';
			}
			g_free(*(char **)ptr);
			*(char **)ptr = str;
			break;
		}
		default:
			assert(false);
		}
	}
}


bool store_to_sqlite(
	sqlite3 *db,
	const char *tablename,
	const char *idcolumn,
	int64_t idvalue,
	const void *src,
	const struct field_desc *fields,
	size_t num_fields,
	GError **err_p)
{
	GString *sql = g_string_sized_new(32 + num_fields * 32);

	/* compose the update statement. */
	g_string_append_printf(sql, "UPDATE %s SET ", tablename);
	for(size_t i=0; i < num_fields; i++) {
		g_string_append_printf(sql, "%s%s = ?",
			i > 0 ? ", " : "", fields[i].column);
	}
	g_string_append_printf(sql, " WHERE %s = ?", idcolumn);

	sqlite3_stmt *stmt = NULL;
	int n = sqlite3_prepare_v2(db, sql->str, sql->len, &stmt, NULL);
	if(n != SQLITE_OK) goto fail;
	format_to_sqlite(stmt, src, fields, num_fields);
	sqlite3_bind_int64(stmt, num_fields, idvalue);
	n = sqlite3_step(stmt);
	if(n != SQLITE_DONE) goto fail;
	if(sqlite3_changes(db) < 1) {
		sqlite3_finalize(stmt);
		g_string_truncate(sql, 0);

		/* compose an insert statement. */
		g_string_append_printf(sql, "INSERT INTO %s (", tablename);
		for(size_t i=0; i < num_fields; i++) {
			g_string_append_printf(sql, "%s%s", i > 0 ? ", " : "",
				fields[i].column);
		}
		g_string_append_printf(sql, "%s%s", num_fields > 0 ? ", " : "",
			idcolumn);
		g_string_append(sql, ") VALUES (");
		for(size_t i=0; i < num_fields; i++) {
			g_string_append_printf(sql, "%s?", i > 0 ? ", " : "");
		}
		g_string_append_printf(sql, "%s?", num_fields > 0 ? ", " : "");
		g_string_append(sql, ")");

		n = sqlite3_prepare_v2(db, sql->str, sql->len, &stmt, NULL);
		if(n != SQLITE_OK) goto fail;
		format_to_sqlite(stmt, src, fields, num_fields);
		sqlite3_bind_int64(stmt, num_fields, idvalue);
		n = sqlite3_step(stmt);
		if(n != SQLITE_DONE) goto fail;
	}

	g_string_free(sql, TRUE);
	sqlite3_finalize(stmt);
	return true;

fail:
	g_set_error(err_p, 0, sqlite3_extended_errcode(db), "%s",
		sqlite3_errmsg(db));
	g_string_free(sql, TRUE);
	if(stmt != NULL) sqlite3_finalize(stmt);
	return false;
}
