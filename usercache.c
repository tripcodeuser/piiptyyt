
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <sqlite3.h>
#include <json-glib/json-glib.h>

#include "defs.h"
#include "pt-user-info.h"


#define GET_DB(cache) (struct cache_db *)g_dataset_id_get_data((cache), \
	cache_db_key)


struct cache_db {
	sqlite3 *db;
};


static GQuark cache_db_key = 0;


static void set_sqlite_error(GError **err_p, sqlite3 *db)
{
	g_set_error(err_p, 0, sqlite3_extended_errcode(db),
		"%s", sqlite3_errmsg(db));
}


static PtUserInfo *fetch_user_info(
	struct cache_db *c,
	uint64_t userid,
	GError **err_p)
{
	sqlite3_stmt *stmt = NULL;
	int n = sqlite3_prepare_v2(c->db,
		"SELECT longname,screenname,profile_image_url,protected,verified,following"
			" FROM cached_user_info WHERE id = ?", -1, &stmt, NULL);
	if(n != SQLITE_OK) {
		set_sqlite_error(err_p, c->db);
		if(stmt != NULL) sqlite3_finalize(stmt);
		return NULL;
	}

	sqlite3_bind_int64(stmt, 0, userid);
	n = sqlite3_step(stmt);
	PtUserInfo *u;
	if(n == SQLITE_ROW) {
		u = pt_user_info_new();
		u->id = userid;
		int num_fs = 0;
		const struct field_desc *fs = pt_user_info_get_field_desc(&num_fs);
		format_from_sqlite(u, stmt, fs, num_fs);
	} else if(n == SQLITE_DONE) {
		/* not found. */
		u = NULL;
	} else {
		set_sqlite_error(err_p, c->db);
		u = NULL;
	}
	sqlite3_finalize(stmt);

	return u;
}


static bool load_schema(sqlite3 *db)
{
	fprintf(stderr, "hey, sir-babe-dudeski, you need to go load a schema by hand.\n");
	return false;
}


/* returns new reference. */
static PtUserInfo *new_or_fetch(struct cache_db *c, uint64_t key)
{
	PtUserInfo *ui = fetch_user_info(c, key, NULL);
	if(ui == NULL) {
		/* make up a "that's all" record */
		ui = pt_user_info_new();
		ui->id = key;
		ui->dirty = false;
	}

	return ui;
}


static bool do_sql(sqlite3 *db, const char *sql, GError **err_p)
{
	char *errmsg = NULL;
	int n = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
	if(n != SQLITE_OK) {
		g_set_error(err_p, 0, 0, "%s", errmsg);
		return false;
	} else {
		return true;
	}
}


static bool flush_user_info(
	struct cache_db *c,
	const PtUserInfo *ui,
	GError **err_p)
{
	int n_fields = 0;
	const struct field_desc *user_info_fields = pt_user_info_get_field_desc(
		&n_fields);
	if(!do_sql(c->db, "BEGIN", err_p)
		|| !store_to_sqlite(c->db, "cached_user_info", "id", ui->id, NULL,
				ui, user_info_fields, n_fields, err_p)
		|| !do_sql(c->db, "COMMIT", err_p))
	{
		do_sql(c->db, "ROLLBACK", NULL);
		return false;
	} else {
		return true;
	}
}


static void user_info_flush(
	GObject **objects,
	size_t num_objects,
	gpointer dataptr)
{
	if(num_objects == 0) return;
	struct cache_db *c = dataptr;
	GError *err = NULL;
	/* TODO: stick a transaction bracket around this. */
	for(size_t i=0; i < num_objects; i++) {
		PtUserInfo *inf = PT_USER_INFO(objects[i]);
		assert(err == NULL);
		if(!flush_user_info(c, inf, &err)) {
			g_warning("can't flush user info for %llu: %s",
				(unsigned long long)inf->id, err->message);
			g_error_free(err);
			err = NULL;
		}
	}
}


/* Jenkins' 32-bit integer hash function, via
 * http://www.concentric.net/~Ttwang/tech/inthash.htm
 */
static uint32_t jenkins_int32_hash(uint32_t a)
{
	a = (a+0x7ed55d16) + (a<<12);
	a = (a^0xc761c23c) ^ (a>>19);
	a = (a+0x165667b1) + (a<<5);
	a = (a+0xd3a2646c) ^ (a<<9);
	a = (a+0xfd7046c5) + (a<<3);
	a = (a^0xb55a4f09) ^ (a>>16);
	return a;
}


/* a double jenkins. yeah. */
static uint32_t double_jenkins(uint64_t x)
{
	return jenkins_int32_hash(x & 0xffffffff)
		^ jenkins_int32_hash(x >> 32);
}


static guint uint64_hash(gconstpointer keyptr) {
	return double_jenkins(*(const uint64_t *)keyptr);
}


static gboolean uint64_equal(gconstpointer a, gconstpointer b) {
	return *(const uint64_t *)a == *(const uint64_t *)b;
}


PtCache *user_cache_open(void)
{
	if(cache_db_key == 0) {
		cache_db_key = g_quark_from_static_string("usercache db dataset key");
		assert(cache_db_key != 0);
	}

	char *db_dir = g_build_filename(g_get_user_cache_dir(),
		"piiptyyt", NULL);
	int n = g_mkdir_with_parents(db_dir, 0700);
	if(n != 0) {
		fprintf(stderr, "%s: g_mkdir_with_parents() failed: %s\n",
			__func__, strerror(errno));
		g_free(db_dir);
		return NULL;
	}
	char *db_path = g_build_filename(db_dir, "cache.sqlite3", NULL);
	g_free(db_dir);

	struct cache_db *c = g_new(struct cache_db, 1);
	n = sqlite3_open_v2(db_path, &c->db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	g_free(db_path);
	if(n != SQLITE_OK) {
		fprintf(stderr, "%s: sqlite3_open_v2 failed: %s\n", __func__,
			sqlite3_errmsg(c->db));
		goto fail;
	}

	/* see if the tables need to be initialized. this should return a "not
	 * found" error.
	 */
	GError *err = NULL;
	PtUserInfo *test_user = fetch_user_info(c, 1, &err);
	if(test_user == NULL && err != NULL
		&& strstr(err->message, "no such table") != NULL)
	{
		if(!load_schema(c->db)) {
			/* FIXME: propagate error */
			g_error_free(err);
			goto fail;
		}
	} else if(test_user != NULL) {
		g_object_unref(test_user);
	}
	if(err != NULL) g_error_free(err);

	PtCache *cache = g_object_new(PT_CACHE_TYPE,
		"high-watermark", 1000, "low-watermark", 600,
		"hash-fn", &uint64_hash,
		"equal-fn", &uint64_equal,
		"flush-fn", &user_info_flush,
		"flush-data", c,
		NULL);
	g_dataset_id_set_data(cache, cache_db_key, c);

	return cache;

fail:
	sqlite3_close(c->db);
	g_free(c);
	return NULL;
}


void user_cache_close(PtCache *cache)
{
	struct cache_db *c = GET_DB(cache);
	g_return_if_fail(c != NULL);

	g_dataset_id_remove_data(c, cache_db_key);
	sqlite3_close(c->db);
	g_free(c);

	g_object_unref(cache);
}


PtUserInfo *get_user_info(PtCache *cache, uint64_t uid)
{
	PtUserInfo *inf;
	GObject *val = pt_cache_get(cache, &uid);
	if(val != NULL) inf = PT_USER_INFO(val);
	else {
		inf = new_or_fetch(GET_DB(cache), uid);
		/* FIXME: strengthen the pt_cache_put() interface so that the
		 * following code has spec-properly borrowed the cache's reference and
		 * not just de facto.
		 */
		pt_cache_put(cache, &inf->id, 0, G_OBJECT(inf));
		g_object_unref(inf);
	}
	return inf;
}


/* fetch user info;
 * - if not present, parse from object
 * - otherwise, update it and set the dirty flag when the object's data
 *   differs from stored
 *
 * this function's purpose is a bit confused. the design isn't at all clean. i
 * blame the pipeweed.
 */
PtUserInfo *get_user_info_from_json(PtCache *cache, JsonObject *obj)
{
	uint64_t uid = json_object_get_int_member(obj, "id");
	if(uid == 0) return NULL;

	PtUserInfo *inf;
	GObject *ent = pt_cache_get(cache, &uid);
	if(ent != NULL) inf = PT_USER_INFO(ent);
	else {
		inf = get_user_info(cache, uid);
		if(inf == NULL) return NULL;
	}

	bool bare = (inf->screenname == NULL);
	bool changed = true;	/* TODO: hunt */
	GError *err = NULL;
	/* FIXME: on failure pt_user_info_from_json() may leave *inf in a halfway
	 * state. really what should be done is deserialize into a distinct object
	 * and copy changed values one at a time, returning number of such fields.
	 */
	if(!pt_user_info_from_json(inf, obj, &err)) {
		g_warning("%s: parsing user info json: %s", __func__, err->message);
		g_error_free(err);

		/* should remove the item from the cache. (store a NULL on top?)
		 * but see fixme above.
		 */
		inf = NULL;
	} else {
		inf->dirty = inf->dirty || bare || changed;
	}

	return inf;
}
