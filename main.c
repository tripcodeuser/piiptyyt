
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <gcrypt.h>
#include <libsoup/soup.h>

#include "defs.h"
#include "pt-update.h"


#define UPDATE_INTERVAL_MSEC (15 * 1000)

#define ERROR_FAIL(err) toplevel_err(__FILE__, __LINE__, __func__, (err))


static void toplevel_err(
	const char *file,
	int line,
	const char *func,
	GError *err)
{
	fprintf(stderr, "%s:%s:%d: %s (code %d)\n", file, func, line,
		err != NULL ? err->message : "(no message)",
		err != NULL ? err->code : 0);
	abort();
}


static bool read_config(void)
{
	bool ok = true;
#if 0
	GError *err = NULL;
	char *cfg_path = g_build_filename(g_get_user_config_dir(),
		"piiptyyt", "config", NULL);
	GKeyFile *kf = g_key_file_new();
	if(!g_key_file_load_from_file(kf, cfg_path, 0, &err)) {
		if(err->code != ENOENT) {
			fprintf(stderr, "can't load config: %s (code %d)\n",
				err->message, err->code);
			ok = false;
		} else {
			/* fill defaults in. */
			twitter_username = NULL;
		}
	} else {
		/* FIXME: move these into a "per-account" group, specialized by
		 * service
		 */
		twitter_username = g_key_file_get_string(kf, "auth", "username", NULL);
	}

	g_free(cfg_path);
	if(err != NULL) g_error_free(err);
	g_key_file_free(kf);
#endif
	return ok;
}


GtkBuilder *load_ui(void)
{
	GtkBuilder *b = gtk_builder_new();
	GError *err = NULL;
	unsigned n = gtk_builder_add_from_file(b, "gui.glade", &err);
	if(n == 0) ERROR_FAIL(err);
	return b;
}


GObject *ui_object(GtkBuilder *b, const char *id)
{
	GObject *o = gtk_builder_get_object(b, id);
	if(o == NULL) {
		GError *e = g_error_new(0, 0, "can't find object by id `%s'", id);
		ERROR_FAIL(e);
	}
	return o;
}


static gboolean main_wnd_delete(GtkWidget *obj, GdkEvent *ev, void *data) {
	/* geez. this sure feels like bureaucracy right here */
	return FALSE;
}


/* FIXME: move this into twitterapi.c or some such */
SoupMessage *make_resource_request_msg(
	const char *uri,
	struct piiptyyt_state *state,
	...)
{
	struct oauth_request *oa = oa_req_new_with_params(
		CONSUMER_KEY, CONSUMER_SECRET, uri, "GET", SIG_HMAC_SHA1, NULL);
	oa_set_token(oa, state->auth_token, state->auth_secret);

	va_list al;
	va_start(al, state);
	for(;;) {
		const char *key = va_arg(al, const char *);
		if(key == NULL) break;
		const char *value = va_arg(al, const char *);
		oa_set_extra_param(oa, key, value);
	}
	va_end(al);

	if(!oa_sign_request(oa, OA_REQ_RESOURCE)) {
		/* FIXME */
		oa_req_free(oa);
		return NULL;
	}

	SoupMessage *msg = soup_message_new("GET", uri);
	GHashTable *query = oa_request_params(oa, OA_REQ_RESOURCE);
	soup_uri_set_query_from_form(soup_message_get_uri(msg), query);
	g_hash_table_destroy(query);

	oa_req_free(oa);
	return msg;
}


/* returned GPtrArray has a null-safe free-function for convenience. */
static GPtrArray *parse_update_array(
	const void *data,
	size_t length,
	struct user_cache *uc,
	GError **err_p)
{
	JsonParser *parser = json_parser_new();
	gboolean ok = json_parser_load_from_data(parser, data, length, err_p);
	if(!ok) {
		g_object_unref(parser);
		return NULL;
	}

	/* FIXME: add checks under each "get" call, since this data comes from an
	 * untrusted source.
	 */
	JsonArray *list = json_node_get_array(
		json_parser_get_root(parser));
	GList *contents = json_array_get_elements(list);
	GPtrArray *updates = g_ptr_array_new_with_free_func(
		(GDestroyNotify)&g_object_unref);
	for(GList *cur = g_list_first(contents);
		cur != NULL;
		cur = g_list_next(cur))
	{
		PtUpdate *u = pt_update_new_from_json(
			json_node_get_object(cur->data), uc, err_p);
		if(u == NULL) {
			g_ptr_array_free(updates, TRUE);
			updates = NULL;
			break;
		}

		g_ptr_array_add(updates, u);
	}

	g_list_free(contents);
	g_object_unref(parser);

	return updates;
}


static void fetch_more_updates(
	struct piiptyyt_state *state,
	struct user_cache *uc,
	struct update_model *model,
	size_t max_count,
	uint64_t low_update_id)
{
	/* experimental: fetch 20 most recent twates, parse them, output something
	 * about them.
	 */
	const char *status_uri = "https://api.twitter.com/1/statuses/home_timeline.json";
	SoupSession *ss = soup_session_async_new();
	SoupMessage *msg = make_resource_request_msg(status_uri, state, NULL);
	soup_session_send_message(ss, msg);
	if(msg->status_code != SOUP_STATUS_OK) {
		fprintf(stderr, "could not get twet: %d %s\n", msg->status_code,
			soup_status_get_phrase(msg->status_code));
	} else {
		GError *err = NULL;
		GPtrArray *updates = parse_update_array(msg->response_body->data,
			msg->response_body->length, uc, &err);
		if(updates == NULL) {
			fprintf(stderr, "can't parse updates: %s\n", err->message);
			g_error_free(err);
		} else {
			add_updates_to_model(model, (struct update **)updates->pdata,
				updates->len);
			g_ptr_array_free(updates, TRUE);
		}
	}
	g_object_unref(msg);
	g_object_unref(ss);
}


struct update_interval_ctx
{
	guint event_name;
};


static gboolean on_update_interval(gpointer dataptr)
{
	struct update_interval_ctx *ctx = dataptr;

	printf("would fetch more tweates here\n");

// end:
	/* wait another interval from the end of this process, rather than from
	 * the beginning.
	 */
	ctx->event_name = g_timeout_add(UPDATE_INTERVAL_MSEC,
		&on_update_interval, ctx);
	return FALSE;
}


int main(int argc, char *argv[])
{
	g_thread_init(NULL);
	gtk_init(&argc, &argv);

	if(!gcry_check_version(GCRYPT_VERSION)) {
		fprintf(stderr, "libgcrypt version mismatch!\n");
		return EXIT_FAILURE;
	}
	gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

	GtkBuilder *b = load_ui();

	/* FIXME: error checks */
	struct piiptyyt_state *state = state_read(NULL);
	if(state == NULL) {
		state = state_empty();
		state_write(state, NULL);
	}

	if(!read_config()) {
		fprintf(stderr, "config read error!\n");
		return EXIT_FAILURE;
	}

	/* FIXME: stick this in a global somewhere */
	struct user_cache *uc = user_cache_open();
	if(uc == NULL) {
		fprintf(stderr, "can't open user cache!\n");
		return EXIT_FAILURE;
	}

	GtkListStore *tweet_model = GTK_LIST_STORE(ui_object(b, "tweet_model"));
	gtk_list_store_clear(tweet_model);

	GObject *main_wnd = ui_object(b, "piiptyyt_main_wnd");
	g_signal_connect(main_wnd, "delete-event",
		G_CALLBACK(&main_wnd_delete), NULL);
	g_signal_connect(main_wnd, "destroy",
		G_CALLBACK(&gtk_main_quit), NULL);
	gtk_widget_show(GTK_WIDGET(main_wnd));

	if(state->auth_token == NULL || state->auth_token[0] == '\0') {
		char *username, *token, *secret;
		uint64_t userid;
		GError *err = NULL;
		if(oauth_login(b, &username, &token, &secret, &userid, &err)) {
			g_free(state->auth_token); state->auth_token = token;
			g_free(state->auth_secret); state->auth_secret = secret;
			g_free(state->username); state->username = username;
			state->userid = userid;
			printf("got new oauth tokens.\n");
			state_write(state, NULL);
			printf("tokens saved.\n");
		} else {
			printf("login failed: %s (code %d).\n", err->message, err->code);
			g_error_free(err);
			/* FIXME: have a retry policy. */
			return EXIT_FAILURE;
		}
	}

	g_object_unref(G_OBJECT(b));
	b = NULL;

	struct update_model *model = update_model_new(tweet_model);
	fetch_more_updates(state, uc, model, 20, 0);

	struct update_interval_ctx *uictx = g_new(struct update_interval_ctx, 1);
	uictx->event_name = g_timeout_add(UPDATE_INTERVAL_MSEC,
		&on_update_interval, uictx);

	gtk_main();

	gboolean ok = g_source_remove(uictx->event_name);
	if(!ok) g_debug("ui refresh timeout %u not found", uictx->event_name);

	/* TODO: check errors etc */
	state_write(state, NULL);
	state_free(state);
	user_cache_close(uc);
	update_model_free(model);

	return EXIT_SUCCESS;
}
