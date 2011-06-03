
PKGS=gtk+-2.0 libsoup-2.4 json-glib-1.0 sqlite3

CFLAGS:=-std=gnu99 -Wall -O1 -g $(shell pkg-config --cflags $(PKGS)) \
	$(shell libgcrypt-config --cflags)
LDFLAGS=
LIBS:=$(shell pkg-config --libs $(PKGS)) -lgnutls \
	$(shell libgcrypt-config --libs)

TARGETS=piiptyyt tags

.PHONY: all clean distclean


all: $(TARGETS)

clean:
	rm -f *.o

distclean: clean
	rm -f $(TARGETS)

tags: $(wildcard *.[ch])
	@ctags -R .


piiptyyt: main.o state.o login.o oauth.o update.o usercache.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)


# FIXME: add auto dependency generation, collection
# (or just move shit over to automake, idgaf)
main.o: main.c defs.h
state.o: state.c defs.h
login.o: login.c defs.h
oauth.o: oauth.c defs.h
update.o: update.c defs.h
usercache.o: usercache.c defs.h
