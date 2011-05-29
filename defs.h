
#ifndef SEEN_DEFS_H
#define SEEN_DEFS_H

#include <stdbool.h>
#include <glib.h>


/* persistent, named client state. */
struct piiptyyt_state
{
	char *username, *auth_token;
};


/* from state.c */

extern struct piiptyyt_state *state_read(GError **err_p);
extern bool state_write(const struct piiptyyt_state *st, GError **err_p);
extern void state_free(struct piiptyyt_state *state);


#endif
