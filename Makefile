
CC      ?= c99
CFLAGS  ?= -O3 -std=c99
HEADERS ?= -DNCURSESW_INCLUDE_H="<ncursesw/curses.h>"
LIBPATH ?=
DESTDIR ?= /usr/local

all: mtm

mtm: tmt.c mtm.c
	$(CC) $(CFLAGS) -o $@ $(HEADERS) tmt.c mtm.c $(LIBPATH) -lncursesw -lutil

install: mtm
	cp mtm $(DESTDIR)/bin
	cp mtm.1 $(DESTDIR)/share/man/man1

clean:
	rm -f *.o mtm
