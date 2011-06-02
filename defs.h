
#ifndef SEEN_DEFS_H
#define SEEN_DEFS_H

#include <stdint.h>
#include <stdbool.h>
#include <glib.h>


/* persistent, named client state. */
struct piiptyyt_state
{
	char *username;
	char *auth_token, *auth_secret;
	uint64_t userid;
};


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
	char **username_p,
	char **auth_token_p,
	char **auth_secret_p,
	uint64_t *userid_p,
	GError **err_p);


/* from oauth.c */

#define SIG_HMAC_SHA1 1

#define OA_REQ_REQUEST_TOKEN 0

struct oauth_request
{
	GStringChunk *strs;
	char *consumer_key, *consumer_secret;
	char *token_key, *token_secret;
	char *request_url, *request_method;
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
extern bool oa_sign_request(struct oauth_request *req, int kind);
extern const char *oa_auth_header(struct oauth_request *req, int kind);
extern GHashTable *oa_request_token_params(struct oauth_request *req);
extern char *oa_request_params_to_post_body(
	struct oauth_request *req,
	int kind);


#endif
