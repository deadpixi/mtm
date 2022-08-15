CC        ?= gcc
CFLAGS    ?= -std=c99 -Wall -Wextra -pedantic -Os
FEATURES  ?= -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
HEADERS   ?=
LIBPATH   ?=
DESTDIR   ?= /usr/local
MANDIR    ?= $(DESTDIR)/share/man/man1
CURSESLIB ?= ncursesw
LIBS      ?= -l$(CURSESLIB) -lutil

all: mtm

mtm: vtparser.c mtm.c pair.c config.h
	$(CC) $(CFLAGS) $(FEATURES) -o $@ $(HEADERS) vtparser.c mtm.c pair.c $(LIBPATH) $(LIBS)
	strip mtm

config.h: config.def.h
	cp -i config.def.h config.h

install: mtm
	mkdir -p $(DESTDIR)/bin $(MANDIR)
	cp mtm $(DESTDIR)/bin
	cp mtm.1 $(MANDIR)

uninstall:
	rm -f $(DESTDIR)/bin/mtm
	rm -f $(MANDIR)/mtm.1

install-terminfo: mtm.ti
	tic -s -x mtm.ti

clean:
	rm -f *.o mtm
