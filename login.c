
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <libsoup/soup.h>

#include "defs.h"


/* for testing: uses OAuth URIs that're served by the toy OAuth
 * implementation, assuming a correctly configured local httpd.
 * no browser will be launched in that case, and the PIN is 12345.
 */
#define USE_LOCAL_CGI 0


struct pin_ctx
{
	bool done;
	gint response;
};


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
	soup_message_set_request(msg, OA_POST_MIME_TYPE,
		SOUP_MEMORY_COPY, body, strlen(body));
#else
	GHashTable *query = oa_request_token_params(oa);
	soup_uri_set_query_from_form(soup_message_get_uri(msg), query);
	g_hash_table_destroy(query);
#endif

	oa_req_free(oa);
	return msg;
}


static SoupMessage *make_access_token_request_msg(
	const char *uri,
	const char *req_token,
	const char *req_secret,
	const char *verifier,
	const char *consumer_key,
	const char *consumer_secret)
{
	struct oauth_request *oa = oa_req_new_with_params(consumer_key,
		consumer_secret, uri, "POST", SIG_HMAC_SHA1, NULL);
	oa_set_token(oa, req_token, req_secret);
	oa_set_verifier(oa, verifier);
	if(!oa_sign_request(oa, OA_REQ_ACCESS_TOKEN)) {
		/* FIXME: do something */
		printf("arashgrjhagkhfasfa\n");
		oa_req_free(oa);
		return NULL;
	}

	SoupMessage *msg = soup_message_new("POST", uri);
	char *body = oa_request_params_to_post_body(oa, OA_REQ_ACCESS_TOKEN);
	soup_message_set_request(msg, OA_POST_MIME_TYPE,
		SOUP_MEMORY_COPY, body, strlen(body));

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


static int launch_authorization_browser(const char *req_token)
{
	pid_t child = fork();
	if(child == 0) {
		const char *auth_uri = "https://api.twitter.com/oauth/authorize";
		char *browser_uri = g_strdup_printf("%s?oauth_token=%s", auth_uri,
			req_token);
		char *argv[] = { "x-www-browser", browser_uri, NULL };
		execvp(argv[0], argv);
		fprintf(stderr, "failed to launch www browser: %s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	} else {
		/* clean up after. */
		int status = 0;
		pid_t p = waitpid(child, &status, 0);
		if(p == (pid_t)-1) {
			fprintf(stderr, "waitpid(2) failed: %s\n", strerror(errno));
			/* FIXME: what now? */
			return EXIT_FAILURE;
		} else {
			return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
		}
	}
}


bool oauth_login(
	GtkBuilder *builder,
	char **username_p,
	char **auth_token_p,
	char **auth_secret_p,
	uint64_t *userid_p,
	GError **err_p)
{
#if !USE_LOCAL_CGI
	const char *token_uri = "https://api.twitter.com/oauth/request_token";
	const char *access_uri = "https://api.twitter.com/oauth/access_token";
#else
	const char *token_uri = "https://localhost/cgi-bin/oauth.cgi/request_token";
	const char *access_uri = "https://localhost/cgi-bin/oauth.cgi/access_token";
#endif

	SoupSession *ss = soup_session_async_new();
#if 0
	SoupLogger *logger = soup_logger_new(1, -1);
	soup_session_add_feature(ss, SOUP_SESSION_FEATURE(logger));
	g_object_unref(logger);
#endif

	SoupMessage *msg = make_token_request_msg(token_uri, CONSUMER_KEY,
		CONSUMER_SECRET);
	if(msg == NULL) {
		/* FIXME */
		abort();
	}
	soup_session_send_message(ss, msg);

	bool ok;
	if(msg->status_code != SOUP_STATUS_OK) {
		fprintf(stderr, "%s: server returned status %d (%s)\n", __func__,
			msg->status_code, soup_status_get_phrase(msg->status_code));
		ok = false;
		goto end;
	}

	/* interpret the response. */
	char **rt_output = oa_parse_response(msg->response_body->data,
		"oauth_token", "oauth_token_secret", NULL);
	if(rt_output == NULL) {
		g_set_error(err_p, 0, EINVAL, "can't parse response data");
		ok = false;
		goto end;
	}

	/* do out-of-band OAuth */
	const char *req_token = rt_output[0], *req_secret = rt_output[1];
#if !USE_LOCAL_CGI
	int rc = launch_authorization_browser(req_token);
	if(rc != EXIT_SUCCESS) {
		fprintf(stderr, "warning: browser launching failed.\n");
	}
#endif

	char *pin = query_pin(builder);
	printf("PIN is: `%s'\n", pin);

	/* get an access token */
	g_object_unref(msg);
	msg = make_access_token_request_msg(access_uri, req_token, req_secret,
		pin, CONSUMER_KEY, CONSUMER_SECRET);
	g_free(pin);
	if(msg == NULL) {
		fprintf(stderr, "can't sign access request message!\n");
		abort();
	}
	soup_session_send_message(ss, msg);

	if(msg->status_code != SOUP_STATUS_OK) {
		fprintf(stderr, "%s: server returned status %d (%s)\n", __func__,
			msg->status_code, soup_status_get_phrase(msg->status_code));
		/* FIXME: handle */
		abort();
	}
	char **at_output = oa_parse_response(msg->response_body->data,
		"oauth_token", "oauth_token_secret", "user_id", "screen_name", NULL);
	if(at_output == NULL) {
		/* FIXME: response data parsing fail */
		abort();
	}
	/* the payoff */
	*auth_token_p = g_strdup(at_output[0]);
	*auth_secret_p = g_strdup(at_output[1]);
	*userid_p = strtoull(at_output[2], NULL, 10);
	*username_p = g_strdup(at_output[3]);

	printf("login successful! username `%s', userid %llu\n", *username_p,
		(unsigned long long)*userid_p);

	g_strfreev(at_output);
	g_strfreev(rt_output);

	ok = true;

end:
	g_object_unref(msg);
	g_object_unref(ss);

	return ok;
}
