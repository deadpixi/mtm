include config.mk

CFLAGS    ?= -std=c99 -Wall -Wextra -pedantic -Os

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
