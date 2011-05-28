
PKGS=gtk+-2.0

CFLAGS:=-std=gnu99 -Wall -O1 -g $(shell pkg-config --cflags $(PKGS))
LDFLAGS=
LIBS:=$(shell pkg-config --libs $(PKGS))

TARGETS=piiptyyt

.PHONY: all clean distclean


all: $(TARGETS)

clean:
	rm -f *.o

distclean: clean
	rm -f $(TARGETS)


piiptyyt: main.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)


main.o: main.c defs.h
