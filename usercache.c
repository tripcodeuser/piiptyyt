
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include <sqlite3.h>

#include "defs.h"


struct user_cache
{
	sqlite3 *db;
	/* TODO: add LRU lists of cached things, i.e. user info, images, etc */
};


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
			goto fail;
		}
	} else if(test_user != NULL) {
		/* FIXME: free it */
	}
	if(err != NULL) g_error_free(err);

	return c;

fail:
	sqlite3_close(c->db);
	g_free(c);
	return NULL;
}


void user_cache_close(struct user_cache *c)
{
	sqlite3_close(c->db);
	g_free(c);
}
