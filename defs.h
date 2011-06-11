
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


/* persistent, named client state. */
struct piiptyyt_state
{
	char *username;
	char *auth_token, *auth_secret;
	uint64_t userid;
};


/* description of a field in a structure. types are
 * 's' for string
 * 'i' for int64_t
 * 'b' for bool
 * 't' for time_t, formatted as a standard UTC timestamp
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


/* the almighty "update", also known as a "tweet" or "status".
 *
 * this structure is a candidate for making into a GObject, for things such as
 * early display of updates that're in reply to uids or sids that the client
 * doesn't remember; a signal would be popped once the contents have settled
 * due to a http transaction completing or data coming down the wire.
 */
struct update
{
	uint64_t id;			/* status id on the microblog service. */
	bool favorited, truncated;
	uint64_t in_rep_to_uid;	/* 0 when not a reply */
	uint64_t in_rep_to_sid;	/* status id, or -''- */
	char *in_rep_to_screen_name;
	char *source;	/* "web", "piiptyyt", etc (should be a local source id) */
	char *text;				/* UTF-8 */

	struct user_info *user;
};


/* from main.c */

extern GObject *ui_object(GtkBuilder *b, const char *id);


/* from update.c */

extern struct update *update_new(void);
extern void update_free(struct update *u);
extern struct update *update_new_from_json(
	JsonObject *obj,
	struct user_cache *uc,		/* if NULL, ret->user == NULL */
	GError **err_p);


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

/* reads prior fields. existing strings are g_free()'d. returns whether a
 * field was changed by input from the JSON object.
 */
extern bool format_from_json(
	void *dest,
	JsonObject *obj,
	const struct field_desc *fields,
	size_t num_fields);

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
