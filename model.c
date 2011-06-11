
#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "defs.h"


static int sort_updates_by_id(const void *ap, const void *bp)
{
	struct update *a = *(struct update *const *)ap,
		*b = *(struct update *const *)bp;
	if(a->id > b->id) return 1;
	else if(a->id < b->id) return -1;
	else return 0;
}


void add_updates_to_model(
	GtkListStore *model,
	struct update **updates,
	size_t num_updates)
{
	GError *err = NULL;
	/* TODO: load this from the update itself */
	GdkPixbuf *av = gdk_pixbuf_new_from_file("img/tablecat.png", &err);
	if(err != NULL) {
		fprintf(stderr, "%s: can't read av image: %s\n", __func__,
			err->message);
		g_error_free(err);
		return;
	}

	qsort(updates, num_updates, sizeof(struct update *),
		&sort_updates_by_id);

	/* FIXME: verify that the first row in the model has a greater id than
	 * updates[num_updates - 1]. if not, use a longer mechanism to generate
	 * item positions.
	 */
	int row_pos[num_updates];
	for(size_t i=0; i < num_updates; i++) row_pos[i] = (int)i;

	for(size_t i=0; i < num_updates; i++) {
		gtk_list_store_insert_with_values(model, NULL, row_pos[i],
			0, g_object_ref(G_OBJECT(av)),
			1, g_strdup(updates[i]->text),		/* FIXME: memory leak */
			-1);
	}
}
