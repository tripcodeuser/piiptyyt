
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
	@rm -rf .deps

tags: $(wildcard *.[ch])
	@ctags -R .


piiptyyt: main.o state.o login.o oauth.o pt-update.o usercache.o format.o \
		model.o
	@echo " LD $@"
	@$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)


%.o: %.c
	@echo " CC $@"
	@test -d .deps || mkdir -p .deps
	@$(CC) -c -o $@ $< $(CFLAGS) -MMD -MF .deps/$(<:.c=.d)

.deps:
	@mkdir -p .deps


include $(wildcard .deps/*)
