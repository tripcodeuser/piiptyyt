
#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>
#include <check.h>

#include "pt-cache.h"


START_TEST(create_and_destroy)
{
	/* FOO */
}
END_TEST


Suite *pt_cache_suite(void)
{
	Suite *s = suite_create("PtCache");

	TCase *tc_foo = tcase_create("foo");
	suite_add_tcase(s, tc_foo);
	tcase_add_test(tc_foo, create_and_destroy);

	return s;
}
