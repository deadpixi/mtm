CC        ?= gcc
CFLAGS    ?= -std=c99 -Wall -Wextra -pedantic -Os
FEATURES  ?= -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
HEADERS   ?=
LIBPATH   ?=
DESTDIR   ?= /usr/local
MANDIR    ?= $(DESTDIR)/share/man/man1

CURSESLIB ?= ncursesw
LIBSDEFAULT ?= -l$(CURSESLIB) -lutil

# Guess CURSESLIB and LIBS but prefer the user choices if possible
NEWCURSES = "$(shell pkg-config --libs curses)"
ifeq ($(.SHELLSTATUS),0)
	CURSESLIB = $(NEWCURSES)
endif

NEWCURSES = "$(shell pkg-config --libs cursesw)"
ifeq ($(.SHELLSTATUS),0)
	CURSESLIB = $(NEWCURSES)
endif

NEWCURSES = "$(shell pkg-config --libs ncurses)"
ifeq ($(.SHELLSTATUS),0)
	CURSESLIB = $(NEWCURSES)
endif

NEWCURSES = "$(shell pkg-config --libs ncursesw)"
ifeq ($(.SHELLSTATUS),0)
	CURSESLIB = $(NEWCURSES)
endif

NEWCURSES = "$(shell pkg-config --libs $(CURSESLIB))"
ifeq ($(.SHELLSTATUS),0)
	CURSESLIB = $(NEWCURSES)
endif

ifeq ("$(shell basename $(shell command -v pkg-config))", "pkg-config")
    LIBS := $(shell pkg-config --libs $(CURSESLIB))
else ("","")
	LIBS := $(LIBSDEFAULT)
endif

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
