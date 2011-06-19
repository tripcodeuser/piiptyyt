
include config.mk

TARGETS=piiptyyt test/testmain tags

.PHONY: all clean distclean check


all: $(TARGETS)

clean:
	rm -f *.o test/*.o

distclean: clean
	rm -f $(TARGETS)
	@rm -rf .deps

check: test/testmain
	test/testmain


tags: $(wildcard *.[ch])
	@ctags -R .


# NOTE: ccan/list/list.c is ignored as the checking functions are never used.
piiptyyt: main.o state.o login.o oauth.o usercache.o format.o \
		model.o pt-update.o pt-user-info.o pt-cache.o
	@echo " LD $@"
	@$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)


test/testmain: test/testmain.o test/pt_cache_suite.o pt-cache.o
	@echo " LD $@"
	@$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS) -lcheck

include $(wildcard .deps/*)
