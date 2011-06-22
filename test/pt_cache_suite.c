
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
	g_object_unref(ret);

	ret = pt_cache_get(cache, "bar");
	fail_unless(ret != NULL);
	fail_unless(ret == second);
	g_object_unref(ret);

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

	fail_unless(*counter == NUM_OBJECTS);
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

	fail_unless(*counter == NUM_OBJECTS);
	g_free(counter);
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

	return s;
}
