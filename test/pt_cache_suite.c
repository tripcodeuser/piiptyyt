
#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>
#include <check.h>

#include "pt-cache.h"


START_TEST(create_put_and_destroy)
{
	GObject *obj = g_object_new(PT_CACHE_TYPE,
		"hash-fn", &g_str_hash,
		"equal-fn", &g_str_equal,
		NULL);
	fail_unless(PT_IS_CACHE(obj));

	PtCache *cache = PT_CACHE(obj);
	fail_if(cache == NULL);

	GObject *trivial = g_object_new(G_TYPE_OBJECT, NULL);
	pt_cache_put(cache, "triviality", 0, trivial);
	g_object_unref(trivial);
	mark_point();

	g_object_unref(obj);
}
END_TEST


START_TEST(create_put_get_and_destroy)
{
	GObject *obj = g_object_new(PT_CACHE_TYPE,
		"hash-fn", &g_str_hash,
		"equal-fn", &g_str_equal,
		"high-watermark", 100,
		NULL);
	PtCache *cache = PT_CACHE(obj);
	fail_unless(cache != NULL);

	GObject *first = g_object_new(G_TYPE_OBJECT, NULL),
		*second = g_object_new(G_TYPE_OBJECT, NULL);
	pt_cache_put(cache, "foo", 0, first);
	pt_cache_put(cache, "bar", 0, second);
	g_object_unref(first);
	g_object_unref(second);

	guint count = 0;
	g_object_get(cache, "count", &count, NULL);
	fail_unless(count == 2);

	GObject *ret = pt_cache_get(cache, "foo");
	fail_unless(ret != NULL);
	fail_unless(ret == first);

	ret = pt_cache_get(cache, "bar");
	fail_unless(ret != NULL);
	fail_unless(ret == second);

	g_object_unref(cache);
}
END_TEST


static void flush_count_cb(GObject **objs, size_t num, gpointer dataptr)
{
	int *counter = dataptr;
	*counter += num;
}


START_TEST(count_flushed_objects)
{
	const int NUM_OBJECTS = 5;

	int *counter = g_new0(int, 1);
	*counter = 0;

	GObject *obj = g_object_new(PT_CACHE_TYPE,
		"flush-fn", &flush_count_cb,
		"flush-data", counter,
		"high-watermark", NUM_OBJECTS + 100,
		NULL);
	PtCache *cache = PT_CACHE(obj);
	fail_unless(cache != NULL);

	for(int i=0; i < NUM_OBJECTS; i++) {
		GObject *o = g_object_new(G_TYPE_OBJECT, NULL);
		pt_cache_put(cache, GINT_TO_POINTER(i + 1), 0, o);
		g_object_unref(o);
	}
	mark_point();

	g_object_unref(cache);
	mark_point();

	fail_unless(*counter >= NUM_OBJECTS);
	g_free(counter);
}
END_TEST


START_TEST(provoke_replacement)
{
	const int NUM_OBJECTS = 1500;

	int *counter = g_new0(int, 1);
	*counter = 0;

	GObject *obj = g_object_new(PT_CACHE_TYPE,
		"flush-fn", &flush_count_cb,
		"flush-data", counter,
		"high-watermark", 150,
		"low-watermark", 100,
		NULL);
	PtCache *cache = PT_CACHE(obj);
	fail_unless(cache != NULL);

	for(int i=0; i < NUM_OBJECTS; i++) {
		GObject *o = g_object_new(G_TYPE_OBJECT, NULL);
		pt_cache_put(cache, GINT_TO_POINTER(i + 1), 0, o);
		g_object_unref(o);
	}
	mark_point();

	fail_unless(*counter > 0, "must have done capacity replacement");

	g_object_unref(cache);
	mark_point();

	fail_unless(*counter >= NUM_OBJECTS);
	g_free(counter);
}
END_TEST


START_TEST(replace_with_linger)
{
	const int NUM_OBJECTS = 3500;

	int *counter = g_new0(int, 1);
	*counter = 0;

	GObject *obj = g_object_new(PT_CACHE_TYPE,
		"flush-fn", &flush_count_cb,
		"flush-data", counter,
		"high-watermark", 150,
		"low-watermark", 100,
		NULL);
	PtCache *cache = PT_CACHE(obj);
	fail_unless(cache != NULL);

	GList *retained = NULL;
	for(int i=0; i < NUM_OBJECTS; i++) {
		GObject *o = g_object_new(G_TYPE_OBJECT, NULL);
		pt_cache_put(cache, GINT_TO_POINTER(i + 1), 0, o);
		if(i % 45 == 0) retained = g_list_prepend(retained, o);
		else g_object_unref(o);

		if(i % 66 == 0) {
			GList *node = g_list_last(retained);
			g_object_unref(node->data);
			retained = g_list_delete_link(retained, node);
		}
	}
	mark_point();

	fail_unless(*counter > 0, "must have done capacity replacement");

	guint count;
	g_object_get(cache, "count", &count, NULL);
	fail_unless(count >= g_list_length(retained));

	g_list_foreach(retained, (GFunc)&g_object_unref, NULL);
	g_list_free(retained);
	mark_point();

	g_object_unref(cache);
	mark_point();

	fail_unless(*counter >= NUM_OBJECTS);
	g_free(counter);
}
END_TEST


START_TEST(flush_on_overwrite)
{
	int *counter = g_new0(int, 1);
	*counter = 0;

	GObject *obj = g_object_new(PT_CACHE_TYPE,
		"flush-fn", &flush_count_cb,
		"flush-data", counter,
		NULL);
	PtCache *cache = PT_CACHE(obj);
	fail_unless(cache != NULL);

	GObject *o = g_object_new(G_TYPE_OBJECT, NULL);
	pt_cache_put(cache, GINT_TO_POINTER(1), 0, o);
	g_object_unref(o);
	*counter = 0;

	o = g_object_new(G_TYPE_OBJECT, NULL);
	pt_cache_put(cache, GINT_TO_POINTER(1), 0, o);
	g_object_unref(o);
	fail_unless(*counter > 0, "overwrite must call flush");

	g_object_unref(cache);
	mark_point();
	g_free(counter);
}
END_TEST


/* check that the cache does reasonable reference counting.
 *
 * missing: overwrite case.
 */
START_TEST(item_destruction)
{
	GObject *obj = g_object_new(PT_CACHE_TYPE, NULL);
	PtCache *cache = PT_CACHE(obj);
	fail_unless(cache != NULL);

	GObject *o = g_object_new(G_TYPE_OBJECT, NULL),
		*o2 = g_object_new(G_TYPE_OBJECT, NULL);
	pt_cache_put(cache, GINT_TO_POINTER(1), 0, o);
	pt_cache_put(cache, GINT_TO_POINTER(2), 0, o2);
	gpointer o_ptr = o, o2_ptr = o2;
	fail_unless(o_ptr != NULL);
	fail_unless(o2_ptr != NULL);
	g_object_add_weak_pointer(o, &o_ptr);
	g_object_add_weak_pointer(o2, &o2_ptr);
	g_object_unref(o);
	g_object_unref(o2);
	mark_point();

	GObject *linger = pt_cache_get(cache, GINT_TO_POINTER(1));
	fail_unless(linger != NULL);
	g_object_ref(linger);

	g_object_unref(cache);
	fail_unless(o_ptr != NULL);
	fail_unless(o2_ptr == NULL);

	g_object_unref(linger);
	fail_unless(o_ptr == NULL);
}
END_TEST


Suite *pt_cache_suite(void)
{
	Suite *s = suite_create("PtCache");

	TCase *tc_iface = tcase_create("interface");
	suite_add_tcase(s, tc_iface);
	tcase_add_test(tc_iface, create_put_and_destroy);
	tcase_add_test(tc_iface, create_put_get_and_destroy);
	tcase_add_test(tc_iface, count_flushed_objects);
	tcase_add_test(tc_iface, provoke_replacement);
	tcase_add_test(tc_iface, replace_with_linger);
	tcase_add_test(tc_iface, flush_on_overwrite);
	tcase_add_test(tc_iface, item_destruction);

	return s;
}
