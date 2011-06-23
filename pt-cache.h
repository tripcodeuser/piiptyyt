
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


/* called when objects are replaced, they are flushed on cache destruction, or
 * they are overwritten in pt_cache_put(). due to lingering references, the
 * flush function may be called on a single object more than once.
 */
typedef void (*PtCacheFlushFunc)(
	GObject **objects,
	size_t num_objects,
	gpointer dataptr);


/* a high/low watermark replacing cache. high and low watermarks default to
 * low tens.
 *
 * construct-only properties:
 *   - "hash-fn" (GHashFunc, defaults to g_direct_hash)
 *   - "equal-fn" (GEqualFunc, defaults to g_direct_equal)
 *   - "flush-fn" (PtCacheFlushFunc, defaults to NULL [not called])
 *   - "flush-data" (gpointer, passed to flush-fn)
 *   - "flush-destroy-notify" (GDestroyNotify for flush-data)
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
	/* items on the active list have an external reference and are subject
	 * only to positive aging (on hit).
	 * items on the inactive list don't, and are eventually replaced.
	 */
	struct list_head active_list, inactive_list;

	GHashFunc hash_fn;
	GEqualFunc equal_fn;
	PtCacheFlushFunc flush_fn;
	gpointer flush_data;
	GDestroyNotify flush_data_destroy_fn;
};


struct _pt_cache_class
{
	GObjectClass parent_class;
};


extern GType pt_cache_get_type(void);

/* NOTE: there's no explicit constructor. create PtCache instances with
 * g_object_new().
 */

/* returns NULL when key isn't found, or a borrowed reference when an item is
 * found. caller should ref the return value to retain it. the borrowed
 * reference is guaranteed to remain valid until the next call to
 * pt_cache_put() on the same cache.
 *
 * key presence can therefore be queried with pt_cache_get(c, k) != NULL .
 */
extern GObject *pt_cache_get(PtCache *cache, gconstpointer key);

/* insert a new object into the cache. if the key exists, it is replaced.
 * creates its own reference from `value'. if `value' is a floating reference,
 * it'll be sunk.
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
