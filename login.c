
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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


char *oauth_login_classic(const char *username, const char *password)
{
	char *ret = NULL;
	GError *err = NULL;

	char *consumer_key = read_consumer_key(&err);
	if(consumer_key == NULL) {
		fprintf(stderr, "%s: failed (%s [code %d])\n", __func__,
			err->message, err->code);
		g_error_free(err);
		return NULL;
	}
	char *consumer_secret = read_consumer_secret(&err);
	if(consumer_secret == NULL) {
		fprintf(stderr, "%s: failed (%s [code %d])\n", __func__,
			err->message, err->code);
		g_error_free(err);
		g_free(consumer_key);
		return NULL;
	}

	const char *token_uri = "https://api.twitter.com/oauth/request_token";
	struct oauth_request *oa = oa_req_new_with_params(
		consumer_key, consumer_secret, token_uri, "POST", SIG_HMAC_SHA1, NULL);
	g_free(consumer_key);
	g_free(consumer_secret);
	if(!oa_sign_request(oa, OA_REQ_REQUEST_TOKEN)) {
		printf("fail!\n");
	}
	oa_req_free(oa);
	return NULL;

	/* for the moment, synchronous operation. this should dip back into the
	 * glib main loop at some point, though.
	 */
	SoupSession *ss = soup_session_sync_new();
	SoupLogger *logger = soup_logger_new(1, -1);
	soup_session_add_feature(ss, SOUP_SESSION_FEATURE(logger));
	g_object_unref(logger);

	SoupMessage *msg = soup_message_new("POST", token_uri);
	soup_message_headers_append(msg->request_headers,
		"Authorization", token_uri);
	soup_session_send_message(ss, msg);
	if(msg->status_code != SOUP_STATUS_OK) {
		fprintf(stderr, "%s: server returned status %d (%s)\n", __func__,
			msg->status_code, soup_status_get_phrase(msg->status_code));
	} else {
		ret = "";
	}
	g_object_unref(msg);

	g_object_unref(ss);

	return ret;
}
