PKGS=gtk+-x11-3.0 libsoup-2.4 json-glib-1.0 sqlite3

CFLAGS:=-std=gnu99 -Wall -O1 -g -I . -I ccan \
	$(shell pkg-config --cflags $(PKGS)) $(shell libgcrypt-config --cflags)
LDFLAGS=
LIBS:=$(shell pkg-config --libs $(PKGS)) -lgnutls \
	$(shell libgcrypt-config --libs)


%.o: %.c
	@echo " CC $@"
	@test -d .deps || mkdir -p .deps
	@$(CC) -c -o $@ $< $(CFLAGS) -MMD -MF .deps/$(subst /,_D_,$(<:.c=.d))

.deps:
	@mkdir -p .deps
