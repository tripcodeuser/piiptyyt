
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
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


struct piiptyyt_state *state_empty(void)
{
	struct piiptyyt_state *state = g_new0(struct piiptyyt_state, 1);
	state->auth_token = NULL;
	state->auth_secret = NULL;
	state->username = NULL;
	state->userid = 0;
	return state;
}


void state_free(struct piiptyyt_state *st)
{
	g_free(st->username);
	g_free(st->auth_token);
	g_free(st->auth_secret);
	g_free(st);
}


bool state_write(const struct piiptyyt_state *st, GError **err_p)
{
	const char *path = state_path();
	int fd = open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
	if(fd == -1) {
		g_set_error(err_p, 0, errno, "open(2) failed: %s",
			g_strerror(errno));
		return false;
	}
	int n = fchmod(fd, 0600);
	if(n == -1) {
		g_set_error(err_p, 0, errno, "fchmod(2) failed: %s",
			g_strerror(errno));
		close(fd);
		return false;
	}

	GKeyFile *kf = g_key_file_new();
	g_key_file_set_string(kf, "auth", "username", st->username);
	g_key_file_set_string(kf, "auth", "auth_token", st->auth_token);
	g_key_file_set_string(kf, "auth", "auth_secret", st->auth_secret);
	g_key_file_set_uint64(kf, "auth", "userid", st->userid);

	gsize length = 0;
	gchar *contents = g_key_file_to_data(kf, &length, err_p);
	g_key_file_free(kf);

	/* FIXME: this should be re-engineered to write into a temporary file in
	 * the same directory with the appropriate mode, and then rename that file
	 * on top of the old file when the initial write has succeeded.
	 *
	 * i.e. this bit should be a function of its own.
	 */
	bool ret = false;
	if(contents != NULL) {
		off_t nn = lseek(fd, 0, SEEK_SET);
		if(nn == (off_t)-1) {
			/* FIXME: handle */
		}
		n = ftruncate(fd, length);
		if(n == -1) {
			/* FIXME: handle */
		}
		ssize_t n_wr = write(fd, contents, length);
		if(n_wr == -1) {
			/* FIXME: handle */
		} else if(n_wr != length) {
			/* FIXME: handle */
		} else {
			ret = true;
		}
	}

	close(fd);
	return ret;
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
	st->auth_secret = g_key_file_get_string(kf, "auth", "auth_secret", NULL);
	st->userid = g_key_file_get_uint64(kf, "auth", "userid", NULL);
	g_key_file_free(kf);

	return st;
}
