
CC      ?= c99
CFLAGS  ?= -O3
HEADERS ?= -DNCURSESW_INCLUDE_H="<ncursesw/curses.h>"
LIBPATH ?=

all: mtm

mtm: tmt.c mtm.c
	$(CC) -o $@ $(HEADERS) tmt.c mtm.c $(LIBPATH) -lncursesw -lutil

clean:
	rm -f *.o mtm
