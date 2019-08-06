CC        ?= gcc
CFLAGS    ?= -std=c99 -Wall -Wextra -pedantic -O3
CPPFLAGS  ?=
LDFLAGS   ?=
FEATURES  ?= -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
HEADERS   ?=
DESTDIR   ?= /usr/local
MANDIR    ?= $(DESTDIR)/man/man1
CURSESLIB ?= ncursesw
LIBRARIES ?= -l$(CURSESLIB) -lutil

CPPFLAGS  += $(FEATURES)
LDFLAGS   += $(LIBRARIES)

all: mtm

mtm: vtparser.c mtm.c config.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $(HEADERS) vtparser.c mtm.c

vtparser.c: vttable.c

vttable.c: tablegen
	./tablegen > vttable.c

config.h: config.def.h
	cp -i config.def.h config.h

install: mtm
	cp mtm $(DESTDIR)/bin
	cp mtm.1 $(MANDIR)

install-terminfo: mtm.ti
	tic -s -x mtm.ti

clean:
	rm -f *.o mtm vttable.c tablegen
