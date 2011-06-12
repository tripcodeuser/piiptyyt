
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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
		JsonNode *node = json_object_get_member(obj, name);
		if(node == NULL) {
			/* not present. skip. */
			continue;
		}
		bool null_ok = islower(fields[i].type),
			is_null = json_node_is_null(node);
		if(!null_ok && is_null) {
			g_critical("%s: field `%s' is null, but not allowed to. AAGH",
				__func__, name);
			/* FIXME: leaves old value there, but should instead return an
			 * error
			 */
			continue;
		}
		void *ptr = dest + fields[i].offset;
		switch(tolower(fields[i].type)) {
		case 'i':
			*(uint64_t *)ptr = is_null ? 0 : json_object_get_int_member(obj, name);
			break;
		case 'b':
			*(bool *)ptr = is_null ? false : json_object_get_boolean_member(obj, name);
			break;
		case 's':
			g_free(*(char **)ptr);
			*(char **)ptr = is_null ? NULL : g_strdup(json_object_get_string_member(obj, name));
			break;
		case 't':
			g_error("decoding of time from JSON object, not implemented.");
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
		bool null_ok = islower(fields[i].type);
		switch(tolower(fields[i].type)) {
		case 'i':
			sqlite3_bind_int64(dest, ix, *(const int64_t *)ptr);
			break;
		case 'b':
			sqlite3_bind_int(dest, ix, *(const bool *)ptr ? 1 : 0);
			break;
		case 's': {
			const char *str = *(char *const *)ptr;
			if(str != NULL) {
				sqlite3_bind_text(dest, ix, str, strlen(str),
					SQLITE_TRANSIENT);
			} else if(null_ok) {
				sqlite3_bind_null(dest, ix);
			} else {
				/* FIXME: report */
				g_error("null not ok for string");
			}
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
		/* FIXME: handle non-nullables */
		switch(tolower(fields[i].type)) {
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


static void bind_idvalue(
	sqlite3_stmt *stmt,
	int col_ix,
	int64_t idvalue,
	const char *idvalue_str)
{
	if(idvalue_str != NULL) {
		sqlite3_bind_text(stmt, col_ix, idvalue_str, strlen(idvalue_str),
			SQLITE_STATIC);
	} else {
		sqlite3_bind_int64(stmt, col_ix, idvalue);
	}
}


bool store_to_sqlite(
	sqlite3 *db,
	const char *tablename,
	const char *idcolumn,
	int64_t idvalue,
	const char *idvalue_str,
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
	bind_idvalue(stmt, num_fields, idvalue, idvalue_str);
	n = sqlite3_step(stmt);
	if(n != SQLITE_DONE) goto fail;
	/* sqlite3_total_changes() counts matched rows, not necessarily rows that
	 * got a different value. that's what we want here.
	 */
	if(sqlite3_total_changes(db) < 1) {
		sqlite3_finalize(stmt); stmt = NULL;

		/* compose an insert statement. */
		bool separate_id = true;
		for(size_t i=0; i < num_fields; i++) {
			if(strcmp(fields[i].column, idcolumn) == 0) {
				separate_id = false;
				break;
			}
		}

		g_string_truncate(sql, 0);
		g_string_append_printf(sql, "INSERT INTO %s (", tablename);
		for(size_t i=0; i < num_fields; i++) {
			g_string_append_printf(sql, "%s%s", i > 0 ? ", " : "",
				fields[i].column);
		}
		if(separate_id) {
			g_string_append_printf(sql, "%s%s", num_fields > 0 ? ", " : "",
				idcolumn);
		}
		g_string_append(sql, ") VALUES (");
		for(size_t i=0; i < num_fields; i++) {
			g_string_append_printf(sql, "%s?", i > 0 ? ", " : "");
		}
		if(separate_id) {
			g_string_append_printf(sql, "%s?", num_fields > 0 ? ", " : "");
		}
		g_string_append(sql, ")");

		n = sqlite3_prepare_v2(db, sql->str, sql->len, &stmt, NULL);
		if(n != SQLITE_OK) goto fail;
		format_to_sqlite(stmt, src, fields, num_fields);
		if(separate_id) bind_idvalue(stmt, num_fields, idvalue, idvalue_str);
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
