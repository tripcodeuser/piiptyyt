
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <glib.h>

#include "defs.h"


static const char *state_path(void)
{
	static char *stored = NULL;
	if(stored == NULL) {
		char *dir = g_build_filename(g_get_user_cache_dir(), "piiptyyt", NULL);
		if(!g_file_test(dir, G_FILE_TEST_IS_DIR)) {
			int n = g_mkdir_with_parents(dir, 0755);
			if(n != 0) {
				/* FIXME: handle this properly. it's only likely to come up at
				 * init, but still.
				 */
				fprintf(stderr, "%s: can't create `%s': %s\n",
					__func__, dir, g_strerror(errno));
				abort();
			}
		}

		stored = g_build_filename(dir, "state", NULL);
		g_free(dir);
	}

	return stored;
}


void state_free(struct piiptyyt_state *st)
{
	g_free(st->username);
	g_free(st->auth_token);
	g_free(st);
}


bool state_write(const struct piiptyyt_state *st, GError **err_p)
{
	GKeyFile *kf = g_key_file_new();
	g_key_file_set_string(kf, "auth", "username", st->username);
	g_key_file_set_string(kf, "auth", "auth_token", st->auth_token);

	gsize length = 0;
	gchar *contents = g_key_file_to_data(kf, &length, err_p);
	g_key_file_free(kf);
	if(contents == NULL) return false;

	gboolean ok = g_file_set_contents(state_path(), contents, length, err_p);
	g_free(contents);
	return ok != FALSE;
}


struct piiptyyt_state *state_read(GError **err_p)
{
	gchar *contents = NULL;
	gsize length = 0;
	GError *err = NULL;
	if(!g_file_get_contents(state_path(), &contents, &length, &err)) {
		if(err->code != ENOENT) {
			g_propagate_error(err_p, err);
			return NULL;
		}
		g_error_free(err);
		contents = g_strdup("");
	}

	GKeyFile *kf = g_key_file_new();
	if(!g_key_file_load_from_data(kf, contents, length, 0, err_p)) {
		g_key_file_free(kf);
		return NULL;
	}

	struct piiptyyt_state *st = g_new(struct piiptyyt_state, 1);
	st->username = g_key_file_get_string(kf, "auth", "username", NULL);
	st->auth_token = g_key_file_get_string(kf, "auth", "auth_token", NULL);
	g_key_file_free(kf);

	return st;
}
