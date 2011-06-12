
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "defs.h"
#include "pt-update.h"


#define id_in_model(m, id) (find_id_in_model((m), (id)) != NULL)


static int uint64_desc_cmp(const void *ap, const void *bp)
{
	const uint64_t *a = ap, *b = bp;
	if(*a > *b) return -1;
	else if(*a < *b) return 1;
	else return 0;
}


/* TODO: write this out by hand. it's used often enough. */
static uint64_t *find_id_in_model(struct update_model *model, uint64_t id)
{
	return bsearch(&id, model->current_ids, model->count,
		sizeof(*model->current_ids), &uint64_desc_cmp);
}


static size_t add_to_current_ids(
	uint64_t **dst_p,
	struct update_model *model,
	PtUpdate **src,
	size_t count,
	bool do_sort)
{
	size_t out_count = model->count + count;
	uint64_t *dst = g_new(uint64_t, out_count);
	/* j-j-jam it in */
	for(size_t i=0; i < count; i++) dst[i] = src[(count - 1) - i]->id;
	for(size_t i=0; i < model->count; i++) {
		dst[count + i] = model->current_ids[i];
	}
	if(do_sort) qsort(dst, out_count, sizeof(uint64_t), &uint64_desc_cmp);
	else {
#ifndef NDEBUG
		for(size_t i=1; i < out_count; i++) assert(dst[i-1] >= dst[i]);
#endif
	}
	g_free(*dst_p);
	*dst_p = dst;
	return out_count;
}


static int sort_updates_by_id(const void *ap, const void *bp)
{
	struct update *a = *(struct update *const *)ap,
		*b = *(struct update *const *)bp;
	if(a->id > b->id) return 1;
	else if(a->id < b->id) return -1;
	else return 0;
}


void add_updates_to_model(
	struct update_model *model,
	struct update **updates,
	size_t num_updates)
{
	if(num_updates == 0) return;

	GError *err = NULL;
	/* TODO: load this from the user cache */
	GdkPixbuf *av = gdk_pixbuf_new_from_file("img/tablecat.png", &err);
	if(err != NULL) {
		fprintf(stderr, "%s: can't read av image: %s\n", __func__,
			err->message);
		g_error_free(err);
		return;
	}

	qsort(updates, num_updates, sizeof(struct update *), &sort_updates_by_id);

	/* determine insert positions for these updates. */
	int row_pos[num_updates];
	struct update *dedup[num_updates];
	size_t row_count = 0;

	/* ignore updates whose IDs already exist in the model. */
	for(size_t i=0; i < num_updates; i++) {
		if(model->count == 0 || !id_in_model(model, updates[i]->id)) {
			dedup[row_count++] = updates[i];
		}
	}

	if(model->count == 0 || updates[0]->id > model->current_ids[0]) {
		/* optimized majority case. prepend all elements. */
		for(size_t i=0; i < num_updates; i++) row_pos[i] = 0;
		model->count = add_to_current_ids(&model->current_ids, model,
			updates, num_updates, false);
	} else {
		/* the complex (interleaving) case. */
		const int p = row_count;

		/* recompute current_ids */
		uint64_t old_head = model->current_ids[0];
		model->count = add_to_current_ids(&model->current_ids, model,
			dedup, p, true);

		/* from largest to smallest, record new positions of each. */
		int k = 0;
		while(k < p && dedup[k]->id > old_head) {
			row_pos[k] = k;
			k++;
		}
		while(k < p) {
			uint64_t *in_current = find_id_in_model(model, dedup[k]->id);
			assert(in_current != NULL);
			row_pos[k++] = in_current - model->current_ids;
		}
	}

	for(size_t i=0; i < row_count; i++) {
		gtk_list_store_insert_with_values(model->store, NULL, row_pos[i],
			0, g_object_ref(av),
			1, g_object_ref(dedup[i]),
			-1);
	}
}


static void set_markup_in_cr_from_object(
	GtkTreeViewColumn *col,
	GtkCellRenderer *cell,
	GtkTreeModel *model,
	GtkTreeIter *iter,
	gpointer dataptr)
{
//	struct update_model *m = dataptr;

	GValue val = { 0 };
	gtk_tree_model_get_value(model, iter, 1, &val);
	if(!G_VALUE_HOLDS(&val, G_TYPE_OBJECT)) {
		g_warning("%s: called for type `%s', not `%s'",
			__func__, G_VALUE_TYPE_NAME(&val), g_type_name(G_TYPE_OBJECT));
		goto end;
	}

	GObject *obj = g_value_get_object(&val);
	if(!PT_IS_UPDATE(obj)) {
		g_warning("%s: called for object of type `%s', expected `%s'",
			__func__, g_type_name(G_OBJECT_TYPE(obj)),
			g_type_name(PT_UPDATE_TYPE));
		goto end;
	}

	PtUpdate *update = PT_UPDATE(obj);
	char *markup = NULL;
	g_object_get(update, "markup", &markup, NULL);
	g_object_set(cell, "markup", markup, NULL);
	g_free(markup);

end:
	g_value_unset(&val);
}


struct update_model *update_model_new(
	GtkTreeView *view,
	GtkListStore *store)
{
	struct update_model *m = g_new(struct update_model, 1);
	m->count = 0;
	m->current_ids = NULL;
	m->store = g_object_ref(store);
	m->view = g_object_ref(view);

	GtkTreeViewColumn *upd_column = gtk_tree_view_get_column(view, 1);
	GList *rs_list = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(upd_column));
	if(rs_list != NULL) {
		m->update_col_r = g_object_ref(
			GTK_CELL_RENDERER(g_list_first(rs_list)->data));
		g_list_free(rs_list);
		gtk_tree_view_column_set_cell_data_func(upd_column,
			m->update_col_r, &set_markup_in_cr_from_object, m, NULL);
	}

	return m;
}


void update_model_free(struct update_model *model)
{
	g_object_unref(model->store);
	g_object_unref(model->view);
	g_object_unref(model->update_col_r);
	g_free(model->current_ids);
	g_free(model);
}
