
#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>
#include <check.h>


extern Suite *pt_cache_suite(void);


int main(void)
{
	g_type_init();

	SRunner *sr = srunner_create(pt_cache_suite());
#if 0
	/* for valgrinding */
	srunner_set_fork_status(sr, CK_NOFORK);
#endif
	srunner_run_all(sr, CK_NORMAL);
	int nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	return 0;
}
