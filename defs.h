
#ifndef SEEN_DEFS_H
#define SEEN_DEFS_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <sqlite3.h>


/* app-wide consumer key, consumer secret. */
#define CONSUMER_KEY "Q61Cbri9yBndDmUFaqz37g"
#define CONSUMER_SECRET "7j4YTFcT9a0rWLPKLpQMwwGeJK40yiHUb5JlzVVvjs4"

#define PT_ERR_DOM (piiptyyt_error_domain())


/* persistent, named client state. */
struct piiptyyt_state
{
	char *username;
	char *auth_token, *auth_secret;
	uint64_t userid;
};


/* description of a field in a structure. types are
 * 's' for string (default NULL)
 * 'i' for int64_t (default 0)
 * 'b' for bool (default false)
 * 't' for time_t, formatted as a standard UTC timestamp (default 1 jan 1970)
 *
 * capitalize letters to pop an error on NULL input, or to disallow storing of
 * NULL strings to sqlite. default values may appear where NULL is allowed.
 */
struct field_desc
{
	const char *name;	/* name in JSON */
	const char *column;	/* column name in SQL database */
	off_t offset;		/* field offset */
	char type;			/* see above */
};

#define SQL_FIELD(s, type, fieldname, name, colname) \
	{ (name), (colname), offsetof(s, fieldname), (type) }
#define FIELD(s, t, fn, name) SQL_FIELD(s, t, fn, name, name)
#define FLD(s, type, name) FIELD(s, type, name, #name)


struct user_cache;
struct update;

/* an empty user_info occurs when the client has only seen a trimmed user JSON
 * object. those can be recognized by ->screenname == NULL.
 */
struct user_info
{
	uint64_t id;
	char *longname, *screenname;
	char *profile_image_url;
	bool protected, verified, following;

	/* not from JSON, but in database */
	char *cached_img_name;
	time_t cached_img_expires;

	/* non-database, non-json fields */
	struct user_cache *cache_parent;
	bool dirty;		/* sync to database on destroy? */
};


/* update display data model.
 *
 * downsides: changing current_ids is always an O(n) operation, where n =
 * max(num_updates, num_updates'). this isn't really significant with fewer
 * than 2k updates, as that will significantly eject other data from a 16k L1
 * data cache.
 *
 * TODO: make this private, move it into model.c entirely
 */
struct update_model
{
	int count;
	uint64_t *current_ids;	/* largest first, always sorted */
	GtkListStore *store;
	GtkTreeView *view;
	GtkCellRenderer *update_col_r, *pic_col_r;
};


/* from main.c */

extern GObject *ui_object(GtkBuilder *b, const char *id);
extern GQuark piiptyyt_error_domain(void);


/* from model.c */

extern struct update_model *update_model_new(
	GtkTreeView *view,
	GtkListStore *store);
extern void update_model_free(struct update_model *model);

extern void add_updates_to_model(
	struct update_model *model,
	struct update **updates,
	size_t num_updates);


/* from usercache.c */

extern struct user_cache *user_cache_open(void);
extern void user_cache_close(struct user_cache *uc);
extern struct user_info *user_info_get(
	struct user_cache *uc,
	uint64_t id);
extern struct user_info *user_info_get_from_json(
	struct user_cache *uc,
	JsonObject *userinfo_obj);
extern void user_info_put(struct user_info *info);

/* gets a GdkPixbuf reference, or returns NULL when it's not available. */
extern GdkPixbuf *user_info_get_userpic(struct user_info *info);


/* from state.c */

extern struct piiptyyt_state *state_empty(void);
extern struct piiptyyt_state *state_read(GError **err_p);
extern bool state_write(const struct piiptyyt_state *st, GError **err_p);
extern void state_free(struct piiptyyt_state *state);


/* from login.c */

/* returns false, fills in *err_p (when not NULL) on error. on success returns
 * true and fills in the other parameters. output strings are caller-free.
 */
extern bool oauth_login(
	GtkBuilder *builder,
	char **username_p,
	char **auth_token_p,
	char **auth_secret_p,
	uint64_t *userid_p,
	GError **err_p);


/* from format.c */

/* reads prior fields. existing strings are g_free()'d. returns true on
 * success, false [with error] on failure.
 */
extern bool format_from_json(
	void *dest,
	JsonObject *obj,
	const struct field_desc *fields,
	size_t num_fields,
	GError **err_p);

/* the sqlite formatters interpret the `fields' array as a sequence of fields
 * in the structure that correspond to columns, or parameters, in offsets
 * [0..num_fields).
 */
extern void format_to_sqlite(
	sqlite3_stmt *dest,
	const void *src,
	const struct field_desc *fields,
	size_t num_fields);

extern void format_from_sqlite(
	void *dst,
	sqlite3_stmt *src,
	const struct field_desc *fields,
	size_t num_fields);

extern bool store_to_sqlite(
	sqlite3 *dest,
	const char *tablename,
	const char *idcolumn,
	int64_t idvalue,
	const char *idvalue_str,
	const void *src,
	const struct field_desc *fields,
	size_t num_fields,
	GError **err_p);


/* from oauth.c */

#define OA_POST_MIME_TYPE "application/x-www-form-urlencoded"

#define SIG_HMAC_SHA1 1

#define OA_REQ_REQUEST_TOKEN 0
#define OA_REQ_ACCESS_TOKEN 1
#define OA_REQ_RESOURCE 2

struct oauth_request
{
	GStringChunk *strs;
	GHashTable *extra_params;
	char *consumer_key, *consumer_secret;
	char *token_key, *token_secret;
	char *request_url, *request_method;
	char *verifier;
	int sig_method;
	char *timestamp, *nonce, *callback_url;
	char *signature;
};

extern void oa_req_free(struct oauth_request *req);
extern struct oauth_request *oa_req_new(void);
extern struct oauth_request *oa_req_new_with_params(
	const char *consumer_key, const char *consumer_secret,
	const char *request_url, const char *request_method,
	int sig_method,
	const char *callback_url);
extern void oa_set_token(
	struct oauth_request *req,
	const char *token,
	const char *secret);
extern void oa_set_verifier(struct oauth_request *req, const char *verifier);
extern void oa_set_extra_param(
	struct oauth_request *req,
	const char *key,
	const char *value);
extern bool oa_sign_request(struct oauth_request *req, int kind);
extern const char *oa_auth_header(struct oauth_request *req, int kind);
extern GHashTable *oa_request_params(struct oauth_request *req, int kind);
extern char *oa_request_params_to_post_body(
	struct oauth_request *req,
	int kind);
extern char **oa_parse_response(const char *body, ...)
	G_GNUC_NULL_TERMINATED;


#endif
