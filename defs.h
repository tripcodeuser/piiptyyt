
#ifndef SEEN_DEFS_H
#define SEEN_DEFS_H

#include <stdint.h>
#include <stdbool.h>
#include <glib.h>
#include <gtk/gtk.h>


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


/* from main.c */

extern GObject *ui_object(GtkBuilder *b, const char *id);


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


/* from oauth.c */

#define OA_POST_MIME_TYPE "application/x-www-form-urlencoded"

#define SIG_HMAC_SHA1 1

#define OA_REQ_REQUEST_TOKEN 0
#define OA_REQ_ACCESS_TOKEN 1

struct oauth_request
{
	GStringChunk *strs;
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
extern bool oa_sign_request(struct oauth_request *req, int kind);
extern const char *oa_auth_header(struct oauth_request *req, int kind);
extern GHashTable *oa_request_token_params(struct oauth_request *req);
extern char *oa_request_params_to_post_body(
	struct oauth_request *req,
	int kind);
extern char **oa_parse_response(const char *body, ...)
	G_GNUC_NULL_TERMINATED;


#endif
