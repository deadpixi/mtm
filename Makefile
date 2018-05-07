CC        ?= gcc
CFLAGS    ?= -O3 -std=c99 -Wall -Wextra -pedantic
FEATURES  ?= -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
HEADERS   ?=
LIBPATH   ?=
DESTDIR   ?= /usr/local
CURSESLIB ?= ncursesw
LIBS      ?= -l$(CURSESLIB) -lutil

all: mtm

mtm: vtparser.c mtm.c config.h
	$(CC) $(CFLAGS) $(FEATURES) -o $@ $(HEADERS) vtparser.c mtm.c $(LIBPATH) $(LIBS)

config.h: config.def.h
	cp -i config.def.h config.h

install: mtm
	cp mtm $(DESTDIR)/bin
	cp mtm.1 $(DESTDIR)/share/man/man1

install-terminfo: mtm.ti
	tic -s -x mtm.ti

clean:
	rm -f *.o mtm
