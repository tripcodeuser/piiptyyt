
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <glib.h>

#include "defs.h"


#define ERROR_FAIL(err) toplevel_err(__FILE__, __LINE__, __func__, (err))


static void toplevel_err(
	const char *file,
	int line,
	const char *func,
	GError *err)
{
	fprintf(stderr, "%s:%s:%d: %s (code %d)\n", file, func, line,
		err->message, err->code);
	abort();
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


static void inject_test_data(GObject *model_obj)
{
	GtkListStore *store = GTK_LIST_STORE(model_obj);
	gtk_list_store_clear(store);

	GError *err = NULL;
	GdkPixbuf *av = gdk_pixbuf_new_from_file("img/tablecat.png", &err);
	if(err != NULL) ERROR_FAIL(err);

	static const char *things[] = {
		"hello i am table cat",
		"lord of cats, master of tables",
	};
	for(int i=0; i < G_N_ELEMENTS(things); i++) {
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter,
			0, g_object_ref(G_OBJECT(av)),
			1, g_strdup(things[i]),
			-1);
	}

	g_object_unref(G_OBJECT(av));
}


static gboolean main_wnd_delete(GtkWidget *obj, GdkEvent *ev, void *data) {
	/* geez. this sure feels like bureaucracy right here */
	return FALSE;
}


int main(int argc, char *argv[])
{
	g_thread_init(NULL);
	gtk_init(&argc, &argv);
	GtkBuilder *b = load_ui();

	struct piiptyyt_state *state = state_read(NULL);
	if(state == NULL) {
		state = state_empty();
		state_write(state, NULL);
	}
	state_free(state);

	GObject *tweet_model = ui_object(b, "tweet_model");
	inject_test_data(tweet_model);

	GObject *main_wnd = ui_object(b, "piiptyyt_main_wnd");
	g_signal_connect(main_wnd, "delete-event",
		G_CALLBACK(&main_wnd_delete), NULL);
	g_signal_connect(main_wnd, "destroy",
		G_CALLBACK(&gtk_main_quit), NULL);
	gtk_widget_show(GTK_WIDGET(main_wnd));

	g_object_unref(G_OBJECT(b));
	gtk_main();

	return EXIT_SUCCESS;
}
