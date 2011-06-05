
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <sqlite3.h>
#include <json-glib/json-glib.h>

#include "defs.h"


/* this caching mechanism's replacement policy is far too eager considering
 * that a single item falling to 0 references may cause a full disk sync at
 * worst. a reference-counted LRU list with a high/low watermark policy would
 * be more appropriate, and one should be designed and implemented before
 * v1.0. it'll have two use cases in piiptyyt, the second being caching of
 * actual updates.
 */
struct user_cache
{
	sqlite3 *db;
	GCache *user_info_cache;
};


static const struct field_desc user_info_fields[] = {
	FIELD(struct user_info, 's', longname, "name"),
	FIELD(struct user_info, 's', screenname, "screen_name"),
	FLD(struct user_info, 's', profile_image_url),
	FLD(struct user_info, 'b', protected),
	FLD(struct user_info, 'b', verified),
	FLD(struct user_info, 'b', following),
};


/* used by functions serving user_info_cache */
static struct user_cache *current_cache;


static void set_sqlite_error(GError **err_p, sqlite3 *db)
{
	g_set_error(err_p, 0, sqlite3_extended_errcode(db),
		"%s", sqlite3_errmsg(db));
}


static struct user_info *fetch_user_info(
	struct user_cache *c,
	uint64_t userid,
	GError **err_p)
{
	sqlite3_stmt *stmt = NULL;
	int n = sqlite3_prepare_v2(c->db,
		"SELECT * FROM cached_user_info WHERE id = ?", -1, &stmt, NULL);
	if(n != SQLITE_OK) {
		set_sqlite_error(err_p, c->db);
		if(stmt != NULL) sqlite3_finalize(stmt);
		return NULL;
	}

	sqlite3_bind_int64(stmt, 0, userid);
	n = sqlite3_step(stmt);
	struct user_info *u;
	if(n == SQLITE_ROW) {
		u = g_new0(struct user_info, 1);
		u->id = userid;
		/* FIXME: do whatever */
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


/* functions for user_info_cache */
static gpointer user_info_fetch_or_new(gpointer keyptr)
{
	struct user_cache *c = current_cache;
	uint64_t key = (uintptr_t)keyptr;

	struct user_info *ui = fetch_user_info(c, key, NULL);
	if(ui == NULL) {
		/* make up a "that's all" record */
		ui = g_new0(struct user_info, 1);
		ui->id = key;
		ui->cache_parent = c;
		ui->dirty = false;
	}

	return ui;
}


static void user_info_free(struct user_info *ui)
{
	if(ui == NULL) return;

	fprintf(stderr, "would flush user info for %llu to database.\n",
		(unsigned long long)ui->id);

	g_free(ui->longname);
	g_free(ui->screenname);
	g_free(ui->profile_image_url);
	g_free(ui->cached_img_name);
	g_free(ui);
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


static guint user_info_hash(struct user_info *ui) {
	return double_jenkins(ui->id);
}


static gpointer direct_dup(gpointer ptr) {
	return ptr;
}


static void direct_free(gpointer ptr) {
	/* nothing at all. */
}


struct user_cache *user_cache_open(void)
{
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

	struct user_cache *c = g_new0(struct user_cache, 1);
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
	struct user_info *test_user = fetch_user_info(c, 1, &err);
	if(test_user == NULL && err != NULL
		&& strstr(err->message, "no such table") != NULL)
	{
		if(!load_schema(c->db)) {
			/* FIXME: propagate error */
			g_error_free(err);
			goto fail;
		}
	} else if(test_user != NULL) {
		/* FIXME: free it */
	}
	if(err != NULL) g_error_free(err);

	/* GCache's interface is sort of shitty. this set of functions uses a
	 * module-global variable for context.
	 */
	c->user_info_cache = g_cache_new((GCacheNewFunc)&user_info_fetch_or_new,
		(GCacheDestroyFunc)&user_info_free,
		&direct_dup, &direct_free, &g_direct_hash, (GHashFunc)&user_info_hash,
		&g_direct_equal);

	return c;

fail:
	sqlite3_close(c->db);
	g_free(c);
	return NULL;
}


void user_cache_close(struct user_cache *c)
{
	g_cache_destroy(c->user_info_cache);
	sqlite3_close(c->db);
	g_free(c);
}


struct user_info *user_info_get(struct user_cache *c, uint64_t uid)
{
	gpointer key = (gpointer)(uintptr_t)uid;
	current_cache = c;
	struct user_info *ui = g_cache_insert(c->user_info_cache, key);
	assert(ui->id == uid);
	return ui;
}


/* fetch user info;
 * - if not present, parse from object
 * - otherwise, update it and set the dirty flag when the object's data
 *   differs from stored
 *
 * this function's purpose is a bit confused. the design isn't at all clean. i
 * blame the pipeweed.
 */
struct user_info *user_info_get_from_json(
	struct user_cache *c,
	JsonObject *obj)
{
	uint64_t uid = json_object_get_int_member(obj, "id");
	if(uid == 0) return NULL;
	gpointer key = (gpointer)(uintptr_t)uid;

	current_cache = c;
	struct user_info *ui = g_cache_insert(c->user_info_cache, key);
	bool bare = (ui->screenname == NULL);
	bool changed = format_from_json(ui, obj, user_info_fields,
		G_N_ELEMENTS(user_info_fields));
	ui->dirty = ui->dirty || bare || changed;

	return ui;
}


void user_info_put(struct user_info *ui)
{
	if(ui == NULL) return;

	current_cache = ui->cache_parent;
	g_cache_remove(ui->cache_parent->user_info_cache, ui);
}
