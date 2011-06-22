
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <glib.h>
#include <glib-object.h>
#include <ccan/list/list.h>

#include "pt-cache.h"


struct cache_item
{
	struct list_node link;
	GObject *ref;
	gpointer key;
	size_t key_size;
	uint32_t age;
};


enum prop_names
{
	PROP_HIGH_WM = 1,
	PROP_LOW_WM,
	PROP_COUNT,

	/* ctor-only */
	PROP_HASH_FN,
	PROP_EQUAL_FN,
	PROP_FLUSH_FN,
	PROP_FLUSH_DATA,
	PROP_FLUSH_DESTROY_NOTIFY,

	PROP__LAST
};


static GParamSpec *properties[PROP__LAST] = { NULL, };


GObject *pt_cache_get(PtCache *self, gconstpointer key)
{
	if(self->keys == NULL) return NULL;

	struct cache_item *item = g_hash_table_lookup(self->keys, key);
	if(item == NULL) return NULL;
	else {
		if(item->age < UINT32_MAX) item->age++;
		return g_object_ref(item->ref);
	}
}


static void pt_cache_replace(PtCache *self)
{
	/* TODO */
}


void pt_cache_put(
	PtCache *self,
	gconstpointer key,
	size_t key_size,
	GObject *object)
{
	if(G_UNLIKELY(self->keys == NULL)) {
		if(self->hash_fn == NULL) self->hash_fn = &g_direct_hash;
		if(self->equal_fn == NULL) self->equal_fn = &g_direct_equal;
		self->keys = g_hash_table_new(self->hash_fn, self->equal_fn);
	}

	struct cache_item *item = g_hash_table_lookup(self->keys, key);
	bool ins;
	if(item != NULL) {
		/* recycle the slot and its presence in the hash table & item list. */
		if(item->ref != NULL) g_object_unref(item->ref);
		if(item->key_size > 0) g_free(item->key);
		ins = false;
	} else {
		self->count++;
		if(self->count > self->wm_high) pt_cache_replace(self);

		item = g_slice_new(struct cache_item);
		ins = true;
	}
	item->ref = g_object_ref(object);
	item->key_size = key_size;
	item->key = key_size == 0 ? (gpointer)key : g_memdup(key, key_size);
	item->age = 1;
	if(ins) {
		list_add_tail(&self->item_list, &item->link);
		g_hash_table_insert(self->keys, item->key, item);
	}
}


static void pt_cache_get_property(
	GObject *object,
	guint prop_id,
	GValue *value,
	GParamSpec *spec)
{
	PtCache *self = PT_CACHE(object);
	switch(prop_id) {
	case PROP_HIGH_WM: g_value_set_uint(value, self->wm_high); break;
	case PROP_LOW_WM: g_value_set_uint(value, self->wm_low); break;
	case PROP_COUNT: g_value_set_uint(value, self->count); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, spec);
		break;
	}
}


static void pt_cache_set_property(
	GObject *object,
	guint prop_id,
	const GValue *value,
	GParamSpec *spec)
{
	PtCache *self = PT_CACHE(object);
	switch(prop_id) {
	case PROP_HIGH_WM: self->wm_high = g_value_get_uint(value); break;
	case PROP_LOW_WM: self->wm_low = g_value_get_uint(value); break;

	/* ctor props */
#define PTR(id, field) \
		case id: self->field = g_value_get_pointer(value); break
	PTR(PROP_HASH_FN, hash_fn);
	PTR(PROP_EQUAL_FN, equal_fn);
	PTR(PROP_FLUSH_FN, flush_fn);
	PTR(PROP_FLUSH_DATA, flush_data);
	PTR(PROP_FLUSH_DESTROY_NOTIFY, flush_data_destroy_fn);
#undef PTR

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, spec);
		break;
	}

	if(self->wm_high <= self->wm_low) {
		g_critical("PtCache high-watermark (%zu) <= low-watermark (%zu)",
			self->wm_high, self->wm_low);
		self->wm_high = self->wm_low + 1;
	}
}


static void pt_cache_init(PtCache *self)
{
	self->keys = NULL;
	self->count = 0;
	self->wm_low = 1;
	self->wm_high = 2;
	list_head_init(&self->item_list);
	self->repl_hand = NULL;

	self->hash_fn = NULL;
	self->equal_fn = NULL;
	self->flush_fn = NULL;
	self->flush_data = NULL;
	self->flush_data_destroy_fn = NULL;
}


static void pt_cache_dispose(GObject *object)
{
	PtCache *self = PT_CACHE(object);

	if(self != NULL) {
		if(self->keys != NULL) {
			GPtrArray *flushable = g_ptr_array_new();
			GHashTableIter iter;
			g_hash_table_iter_init(&iter, self->keys);
			gpointer k, v;
			while(g_hash_table_iter_next(&iter, &k, &v)) {
				struct cache_item *item = v;
				g_hash_table_iter_remove(&iter);
				if(item->key_size > 0) g_free(item->key);
				if(item->ref != NULL) g_ptr_array_add(flushable, item->ref);
				g_slice_free(struct cache_item, item);
			}
			assert(g_hash_table_size(self->keys) == 0);

			if(self->flush_fn != NULL) {
				(*self->flush_fn)((GObject **)flushable->pdata,
					flushable->len, self->flush_data);
			}
			g_ptr_array_foreach(flushable, (GFunc)&g_object_unref, NULL);
			g_ptr_array_free(flushable, TRUE);
		}
	}

	GObjectClass *parent_class = g_type_class_peek_parent(
		PT_CACHE_GET_CLASS(self));
	parent_class->dispose(object);
}


static void pt_cache_finalize(GObject *object)
{
	PtCache *self = PT_CACHE(object);

	if(self != NULL) {
		g_hash_table_destroy(self->keys);

		if(self->flush_data_destroy_fn != NULL) {
			(*self->flush_data_destroy_fn)(self->flush_data);
			self->flush_data_destroy_fn = NULL;
		}
	}

	GObjectClass *parent_class = g_type_class_peek_parent(
		PT_CACHE_GET_CLASS(self));
	parent_class->finalize(object);
}


static void pt_cache_class_init(PtCacheClass *klass)
{
	GObjectClass *obj_class = G_OBJECT_CLASS(klass);

	obj_class->finalize = &pt_cache_finalize;
	obj_class->dispose = &pt_cache_dispose;
	obj_class->get_property = &pt_cache_get_property;
	obj_class->set_property = &pt_cache_set_property;

	properties[PROP_HIGH_WM] = g_param_spec_uint("high-watermark",
		"High watermark for replacement",
		"Get and set high watermark value",
		2, UINT_MAX, 50,
		G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

	properties[PROP_LOW_WM] = g_param_spec_uint("low-watermark",
		"Low watermark for replacement",
		"Get and set low watermark value",
		1, UINT_MAX, 30,
		G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

	properties[PROP_COUNT] = g_param_spec_uint("count",
		"Entry count", "Get number of entries in the cache",
		0, UINT_MAX, 0,
		G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/* ctor-only properties */
	properties[PROP_HASH_FN] = g_param_spec_pointer(
		"hash-fn", "hash-function", "GHashFunc for cache keys",
		G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

	properties[PROP_EQUAL_FN] = g_param_spec_pointer(
		"equal-fn", "equal-function", "GEqualFunc for cache keys",
		G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

	properties[PROP_FLUSH_FN] = g_param_spec_pointer(
		"flush-fn", "flush-function",
		"Pre-unref function called on replaced items",
		G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

	properties[PROP_FLUSH_DATA] = g_param_spec_pointer(
		"flush-data", NULL, "userdata pointer for flush-fn",
		G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

	properties[PROP_FLUSH_DESTROY_NOTIFY] = g_param_spec_pointer(
		"flush-destroy-notify", NULL, "GDestroyNotify for flush-data",
		G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties(obj_class, PROP__LAST, properties);
}


G_DEFINE_TYPE(PtCache, pt_cache, G_TYPE_OBJECT);
