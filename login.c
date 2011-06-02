
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <libsoup/soup.h>

#include "defs.h"


static char *read_consumer_key(GError **err_p)
{
	char *path = g_build_filename(g_get_home_dir(),
		"api.twitter.com__consumer_key", NULL);
	char *key = NULL;
	gboolean ok = g_file_get_contents(path, &key, NULL, err_p);
	g_free(path);
	if(ok) g_strchomp(key);
	return key;
}


static char *read_consumer_secret(GError **err_p)
{
	char *path = g_build_filename(g_get_home_dir(),
		"api.twitter.com__consumer_secret", NULL);
	char *key = NULL;
	gboolean ok = g_file_get_contents(path, &key, NULL, err_p);
	g_free(path);
	if(ok) g_strchomp(key);
	return key;
}


static SoupMessage *make_token_request_msg(
	const char *token_uri,
	const char *consumer_key,
	const char *consumer_secret)
{
	struct oauth_request *oa = oa_req_new_with_params(
		consumer_key, consumer_secret, token_uri, "POST", SIG_HMAC_SHA1, NULL);
	if(!oa_sign_request(oa, OA_REQ_REQUEST_TOKEN)) {
		/* FIXME: handle! */
		printf("fail!\n");
		oa_req_free(oa);
		return NULL;
	}

	SoupMessage *msg = soup_message_new("POST", token_uri);
#if 0
	soup_message_headers_append(msg->request_headers,
		"Authorization", oa_auth_header(oa, OA_REQ_REQUEST_TOKEN));
#elif 1
	char *body = oa_request_params_to_post_body(oa, OA_REQ_REQUEST_TOKEN);
	soup_message_set_request(msg, "application/x-www-form-urlencoded",
		SOUP_MEMORY_COPY, body, strlen(body));
#else
	GHashTable *query = oa_request_token_params(oa);
	soup_uri_set_query_from_form(soup_message_get_uri(msg), query);
	g_hash_table_destroy(query);
#endif

	oa_req_free(oa);
	return msg;
}


bool oauth_login(
	char **username_p,
	char **auth_token_p,
	char **auth_secret_p,
	uint64_t *userid_p,
	GError **err_p)
{
	GError *err = NULL;

	/* TODO: replace these with constants */
	char *consumer_key = read_consumer_key(&err);
	if(consumer_key == NULL) {
		fprintf(stderr, "%s: failed (%s [code %d])\n", __func__,
			err->message, err->code);
		g_error_free(err);
		return false;
	}
	char *consumer_secret = read_consumer_secret(&err);
	if(consumer_secret == NULL) {
		fprintf(stderr, "%s: failed (%s [code %d])\n", __func__,
			err->message, err->code);
		g_error_free(err);
		g_free(consumer_key);
		return false;
	}

//	const char *token_uri = "https://api.twitter.com/oauth/request_token";
	const char *token_uri = "https://localhost/cgi-bin/oauth.cgi/request_token";

	SoupSession *ss = soup_session_async_new();
#if 1
	SoupLogger *logger = soup_logger_new(1, -1);
	soup_session_add_feature(ss, SOUP_SESSION_FEATURE(logger));
	g_object_unref(logger);
#endif

	SoupMessage *msg = make_token_request_msg(token_uri, consumer_key,
		consumer_secret);
	soup_session_send_message(ss, msg);

	bool ok;
	if(msg->status_code != SOUP_STATUS_OK) {
		fprintf(stderr, "%s: server returned status %d (%s)\n", __func__,
			msg->status_code, soup_status_get_phrase(msg->status_code));
		ok = false;
	} else {
		/* interpret the response. */
		const char *resp = msg->response_body->data;
		char **pieces = g_strsplit(resp, "&", 0);
		for(int i=0; pieces[i] != NULL; i++) {
			char *eq = strchr(pieces[i], '=');
			if(eq == NULL) {
				fprintf(stderr, "%s: warning: unknown response component `%s'\n",
					__func__, pieces[i]);
				continue;
			}
			*(eq++) = '\0';
			if(strcmp(pieces[i], "oauth_token") == 0) {
				*auth_token_p = g_strdup(eq);
			} else if(strcmp(pieces[i], "oauth_token_secret") == 0) {
				*auth_secret_p = g_strdup(eq);
			} else if(strcmp(pieces[i], "oauth_callback_confirmed") == 0) {
				/* ignoring callback confirmation, since this is a desktop
				 * client and won't have a publicly visible endpoint.
				 */
			} else {
				fprintf(stderr, "%s: warning: unknown response field `%s'\n",
					__func__, pieces[i]);
				continue;
			}
		}
		g_strfreev(pieces);
		ok = true;
	}
	g_object_unref(msg);

	g_object_unref(ss);

	g_set_error(err_p, 0, ENOSYS, "incomplete");
	return false;
}
