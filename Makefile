CC      := gcc
CFLAGS  ?= -g -std=c99 -Wall -Wextra -pedantic
HEADERS ?= -DNCURSESW_INCLUDE_H="<ncursesw/curses.h>"
LIBPATH ?=
DESTDIR ?= /usr/local

all: mtm

mtm: tmt.c mtm.c config.h
	$(CC) $(CFLAGS) -o $@ $(HEADERS) tmt.c mtm.c $(LIBPATH) -lncursesw -lutil

config.h: config.def.h
	cp -i config.def.h config.h

install: mtm
	cp mtm $(DESTDIR)/bin
	cp mtm.1 $(DESTDIR)/share/man/man1

clean:
	rm -f *.o mtm
