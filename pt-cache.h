
#ifndef SEEN_PT_CACHE_H
#define SEEN_PT_CACHE_H

#include <stdlib.h>
#include <stdbool.h>
#include <glib.h>
#include <glib-object.h>
#include <ccan/list/list.h>


#define PT_CACHE_TYPE (pt_cache_get_type())
#define PT_CACHE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), PT_CACHE_TYPE, PtCache))
#define PT_IS_CACHE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), PT_CACHE_TYPE))
#define PT_CACHE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), PT_CACHE_TYPE, PtCacheClass))
#define PT_IS_CACHE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), PT_CACHE_TYPE))
#define PT_CACHE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), PT_CACHE_TYPE, PtCacheClass))


typedef struct _pt_cache PtCache;
typedef struct _pt_cache_class PtCacheClass;


/* a high/low watermark replacing cache. high and low watermarks default to
 * low tens.
 *
 * properties:
 *   - "high-watermark" (rw uint)
 *   - "low-watermark" (rw uint)
 *   - "count" (r uint)
 */
struct _pt_cache
{
	GObject parent_instance;

	size_t wm_low, wm_high, count;
	GHashTable *keys;
	struct list_head item_list;
	struct list_node *repl_hand;
};


struct _pt_cache_class
{
	GObjectClass parent_class;
};


extern GType pt_cache_get_type(void);

/* TODO: to make PtCache derivable, make key_hash and key_equal construct-only
 * properties and create the hashtable lazily.
 */
extern PtCache *pt_cache_new(GHashFunc key_hash, GEqualFunc key_equal);

/* returns NULL when key isn't found, or adds reference to returned GObject
 * reference. caller should unref the object when done.
 */
extern GObject *pt_cache_get(PtCache *cache, gconstpointer key);

/* insert a new object into the cache. if the key exists, it is replaced.
 * creates its own reference from `value'.
 *
 * if key_size is zero, the key pointer is retained as such. if it is greater
 * than zero, key is retained with that size using g_memdup().
 */
extern void pt_cache_put(
	PtCache *cache,
	gconstpointer key,
	size_t key_size,
	GObject *value);

#endif
