
#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>
#include <check.h>


extern Suite *pt_cache_suite(void);


int main(void)
{
	g_type_init();

	SRunner *sr = srunner_create(pt_cache_suite());
	srunner_run_all(sr, CK_NORMAL);
	int nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	return 0;
}
