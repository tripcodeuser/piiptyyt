
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <glib.h>
#include <glib-object.h>
#include <ccan/list/list.h>

#include "pt-cache.h"


#define MAX_REPLACE 128	/* arbitrary. */


struct cache_item
{
	struct list_node link;
	GObject *ref;
	gpointer key;
	size_t key_size;
	PtCache *parent;	/* not a ref */
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


static size_t list_length(struct list_head *head)
{
	size_t acc = 0;
	struct cache_item *child;
	list_for_each(head, child, link) {
		acc++;
	}
	return acc;
}


/* remove an item from its active/inactive list while keeping repl_hand in its
 * correct position.
 */
static void delist_item(struct cache_item *item)
{
	PtCache *cache = item->parent;
	if(&item->link == cache->repl_hand) {
		cache->repl_hand = cache->repl_hand->next;
		if(cache->repl_hand == &cache->inactive_list.n) {
			cache->repl_hand = NULL;
		}
	}
	list_del(&item->link);
	assert(cache->repl_hand == NULL
		|| (cache->repl_hand != &item->link
			&& cache->repl_hand->next != &item->link
			&& cache->repl_hand->prev != &item->link));
}


static void toggle_last_ref_cb(
	gpointer dataptr,
	GObject *obj,
	gboolean is_last)
{
	struct cache_item *item = dataptr;
	assert(item->ref == obj);
	PtCache *cache = item->parent;

	delist_item(item);
	if(!is_last) {
		list_add_tail(&cache->active_list, &item->link);
		cache->active_count++;
	} else if(item->age == 0) {
		/* a last reference on an old object. remove it immediately. */
		if(cache->flush_fn != NULL) {
			(*cache->flush_fn)(&obj, 1, cache->flush_data);
		}
		g_object_remove_toggle_ref(obj, &toggle_last_ref_cb, item);
		assert(g_hash_table_lookup(cache->keys, item->key) == item);
		g_hash_table_remove(cache->keys, item->key);
		if(item->key_size > 0) g_free(item->key);
		cache->count--;
		cache->active_count--;
		g_slice_free(struct cache_item, item);
	} else {
		list_add_tail(&item->parent->inactive_list, &item->link);
		cache->active_count--;
	}

	assert(list_length(&cache->active_list) == cache->active_count);
}


GObject *pt_cache_get(PtCache *self, gconstpointer key)
{
	if(self->keys == NULL) return NULL;

	struct cache_item *item = g_hash_table_lookup(self->keys, key);
	if(item == NULL) return NULL;
	else {
		if(item->age < UINT32_MAX) item->age++;
		assert(item->ref != NULL);
		return item->ref;
	}
}


/* NOTE: this expects to only be fed items either from the inactive list, or
 * from the cache destructor. as such it doesn't need to update active_count.
 */
static void flush_items(PtCache *self, struct cache_item **items, size_t count)
{
	assert(count <= MAX_REPLACE);
	assert(count <= self->count);
	if(count == 0) return;

	if(self->flush_fn != NULL) {
		GObject *objs[MAX_REPLACE];
		for(int i=0; i < count; i++) objs[i] = items[i]->ref;
		(*self->flush_fn)(objs, count, self->flush_data);
	}
	for(int i=0; i < count; i++) {
		struct cache_item *it = items[i];
		delist_item(it);
		if(self->keys != NULL) {
			assert(g_hash_table_lookup(self->keys, it->key) == it);
			g_hash_table_remove(self->keys, it->key);
		}
		if(it->key_size > 0) g_free(it->key);
		g_object_remove_toggle_ref(it->ref, &toggle_last_ref_cb, it);
		g_slice_free(struct cache_item, it);
	}

	self->count -= count;
}


/* replaces items from inactive_list until self->count - self->active_count <=
 * self->wm_low .
 */
static void pt_cache_replace(PtCache *self, int max_iters)
{
	struct list_node *hand = self->inactive_list.n.next;
	if(hand == &self->inactive_list.n) return;	/* empty */
	if(hand == NULL) hand = self->inactive_list.n.next;

	struct cache_item *r_buf[MAX_REPLACE];
	int r_count = 0, iters = 0;
	while(self->count - self->active_count > self->wm_low
		&& iters++ < max_iters)
	{
		struct cache_item *it = list_entry(hand, struct cache_item, link);

		/* advance the hand. */
		hand = hand->next;
		if(hand == &self->inactive_list.n) {
			hand = self->inactive_list.n.next;
		}

		if(it->age > 0) it->age >>= 1;
		else {
			assert(it->age == 0);
			r_buf[r_count++] = it;
			if(r_count == MAX_REPLACE) {
				self->repl_hand = hand;
				flush_items(self, r_buf, r_count);
				hand = self->repl_hand;
				r_count = 0;
			}
		}
	}

	self->repl_hand = hand;
	flush_items(self, r_buf, r_count);
}


static void pt_cache_replace_full(PtCache *self)
{
	size_t before;
	do {
		before = self->count;
		pt_cache_replace(self, self->count - self->active_count);
	} while(self->count - self->active_count > self->wm_low
		&& before != self->count);
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
		if(self->flush_fn != NULL) {
			(*self->flush_fn)(&item->ref, 1, self->flush_data);
		}
		g_object_remove_toggle_ref(item->ref, &toggle_last_ref_cb, item);
		delist_item(item);
		if(item->key_size > 0) g_free(item->key);
		assert(item->parent == self);
		ins = false;
	} else {
		if(self->count >= self->wm_high) pt_cache_replace_full(self);
		self->count++;

		item = g_slice_new(struct cache_item);
		item->parent = self;
		ins = true;
	}
	item->key_size = key_size;
	item->key = key_size == 0 ? (gpointer)key : g_memdup(key, key_size);
	if(ins) g_hash_table_insert(self->keys, item->key, item);
	item->age = 1;
	item->ref = g_object_ref_sink(object);
	/* starts out strong. */
	list_add_tail(&self->active_list, &item->link);
	self->active_count++;
	g_object_add_toggle_ref(item->ref, &toggle_last_ref_cb, item);
	g_object_unref(item->ref);	/* might go passive if sunk. */

	assert(g_hash_table_size(self->keys) == self->count);
	assert(list_length(&self->active_list) + list_length(&self->inactive_list) == self->count);
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
	self->active_count = 0;
	self->wm_low = 1;
	self->wm_high = 2;
	list_head_init(&self->active_list);
	list_head_init(&self->inactive_list);
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

	if(self != NULL && self->keys != NULL) {
		GPtrArray *items = g_ptr_array_new();
		GHashTableIter iter;
		g_hash_table_iter_init(&iter, self->keys);
		gpointer k, v;
		while(g_hash_table_iter_next(&iter, &k, &v)) {
			struct cache_item *item = v;
			assert(k == item->key);
			g_ptr_array_add(items, item);
		}
		g_hash_table_destroy(self->keys);
		self->keys = NULL;

		for(int i=0; i < items->len; i += MAX_REPLACE) {
			struct cache_item **array = (struct cache_item **)items->pdata;
			flush_items(self, &array[i], MIN(items->len - i, MAX_REPLACE));
		}
		g_ptr_array_free(items, TRUE);
	}

	GObjectClass *parent_class = g_type_class_peek_parent(
		PT_CACHE_GET_CLASS(self));
	parent_class->dispose(object);
}


static void pt_cache_finalize(GObject *object)
{
	PtCache *self = PT_CACHE(object);

	if(self != NULL) {
		assert(self->keys == NULL);

		if(self->flush_data_destroy_fn != NULL) {
			(*self->flush_data_destroy_fn)(self->flush_data);
			self->flush_data_destroy_fn = NULL;
			self->flush_data = NULL;
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
