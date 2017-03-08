CC       := gcc
CFLAGS   ?= -g -std=c99 -Wall -Wextra -pedantic
FEATURES ?= -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE_EXTENDED -DTMT_HAS_WCWIDTH
HEADERS  ?=
LIBPATH  ?=
DESTDIR  ?= /usr/local
LIBS     ?= -lncursesw -lutil

all: mtm

mtm: tmt.c mtm.c config.h
	$(CC) $(CFLAGS) $(FEATURES) -o $@ $(HEADERS) tmt.c mtm.c $(LIBPATH) $(LIBS)

config.h: config.def.h
	cp -i config.def.h config.h

install: mtm
	cp mtm $(DESTDIR)/bin
	cp mtm.1 $(DESTDIR)/share/man/man1

clean:
	rm -f *.o mtm
