
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <libsoup/soup.h>

#include "defs.h"


struct pin_ctx
{
	bool done;
	gint response;
};


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


static void on_login_pin_response(
	GtkDialog *dialog,
	gint response,
	struct pin_ctx *pin_ctx)
{
	if(!pin_ctx->done) {
		pin_ctx->done = true;
		pin_ctx->response = response;
	}
}


/* pop up the PIN entry window. */
static char *query_pin(GtkBuilder *builder)
{
	GtkEntry *pin_entry = GTK_ENTRY(ui_object(builder, "login_pin_entry"));
	gtk_entry_set_text(pin_entry, "");

	struct pin_ctx *pin_ctx = g_new(struct pin_ctx, 1);
	pin_ctx->done = false;
	pin_ctx->response = 0;

	GtkButton *complete_btn = GTK_BUTTON(ui_object(builder, "login_pin_ok_btn")),
		*cancel_btn = GTK_BUTTON(ui_object(builder, "login_pin_cancel_btn"));

	GtkDialog *pin_wnd = GTK_DIALOG(ui_object(builder, "login_pin_dialog"));
	gtk_dialog_set_default_response(pin_wnd,
		gtk_dialog_get_response_for_widget(pin_wnd, GTK_WIDGET(cancel_btn)));
	gulong resp = g_signal_connect(G_OBJECT(pin_wnd), "response",
		G_CALLBACK(&on_login_pin_response), pin_ctx);
	gtk_widget_show_all(GTK_WIDGET(pin_wnd));
	gtk_dialog_run(pin_wnd);

	gtk_widget_hide_all(GTK_WIDGET(pin_wnd));
	char *ret;
	if(pin_ctx->done
		&& gtk_dialog_get_widget_for_response(pin_wnd, pin_ctx->response)
			== GTK_WIDGET(complete_btn))
	{
		ret = g_strdup(gtk_entry_get_text(pin_entry));
	} else {
		ret = NULL;
	}

	g_free(pin_ctx);
	g_signal_handler_disconnect(G_OBJECT(pin_wnd), resp);

	return ret;
}


bool oauth_login(
	GtkBuilder *builder,
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
		goto end;
	} else {
		/* interpret the response. */
		char **output = oa_parse_response(msg->response_body->data,
			"oauth_token", "oauth_token_secret", NULL);
		if(output == NULL) {
			g_set_error(err_p, 0, EINVAL, "can't parse response data");
			ok = false;
			goto end;
		}

		printf("token `%s', secret `%s'\n", output[0], output[1]);
		g_strfreev(output);
	}

	char *pin = query_pin(builder);
	printf("PIN is: `%s'\n", pin);
	g_free(pin);

	ok = true;

end:
	g_object_unref(msg);
	g_object_unref(ss);

	ok = false;
	if(err_p != NULL && *err_p == NULL) {
		g_set_error(err_p, 0, ENOSYS, "incomplete");
	}
	return ok;
}
