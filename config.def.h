/* Old versions of ncurses don't support A_ITALIC.
 * Define this to disable it if the situation isn't automatically detected.
#define NO_ITALICS
 */

/* mtm by default will advertise itself as a "screen-bce" terminal.
 * This is the terminal type advertised by such programs as
 * screen(1) and tmux(1) and is a widely-supported terminal type.
 * mtm supports emulating the "screen-bce" terminal very well, and this
 * is a reasonable default.
 *
 * However, you can change the default terminal that mtm will
 * advertise itself as. There's the "mtm" terminal type that is
 * recommended for use if you know it will be available in all the
 * environments in which mtm will be used. It advertises a few
 * features that mtm has that the default "screen-bce" terminfo doesn't
 * list, meaning that terminfo-aware programs may get a small
 * speed boost.
 */
#define DEFAULT_TERMINAL "screen-bce"
#define DEFAULT_256_COLOR_TERMINAL "screen-256color-bce"

/* mtm supports a scrollback buffer, allowing users to scroll back
 * through the output history of a virtual terminal. The SCROLLBACK
 * knob controls how many lines are saved (minus however many are
 * currently displayed). 1000 seems like a good number.
 *
 * Note that every virtual terminal is sized to be at least this big,
 * so setting a huge number here might waste memory. It is recommended
 * that this number be at least as large as the largest terminal you
 * expect to use is tall.
 */
#define SCROLLBACK 1000

/* The default command prefix key, when modified by cntrl.
 * This can be changed at runtime using the '-c' flag.
 */
#define COMMAND_KEY 'g'

/* The change focus keys. */
#define MOVE_UP         CODE(KEY_UP)
#define MOVE_DOWN       CODE(KEY_DOWN)
#define MOVE_RIGHT      CODE(KEY_RIGHT)
#define MOVE_LEFT       CODE(KEY_LEFT)
#define MOVE_OTHER      KEY(L'o')

/* The split terminal keys. */
#define HSPLIT KEY(L'h')
#define VSPLIT KEY(L'v')

/* The delete terminal key. */
#define DELETE_NODE KEY(L'w')

/* The force redraw key. */
#define REDRAW KEY(L'l')

/* The scrollback keys. */
#define SCROLLUP CODE(KEY_PPAGE)
#define SCROLLDOWN CODE(KEY_NPAGE)
#define RECENTER CODE(KEY_END)

/* The path for the wide-character curses library. */
#ifndef NCURSESW_INCLUDE_H
    #if defined(__APPLE__) || !defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
        #define NCURSESW_INCLUDE_H <curses.h>
    #else
        #define NCURSESW_INCLUDE_H <ncursesw/curses.h>
    #endif
#endif
#include NCURSESW_INCLUDE_H

/* Includes needed to make forkpty(3) work. */
#ifndef FORKPTY_INCLUDE_H
    #if defined(__APPLE__) || defined(__OpenBSD__)
        #define FORKPTY_INCLUDE_H <util.h>
    #elif defined(__FreeBSD__)
        #define FORKPTY_INCLUDE_H <libutil.h>
    #else
        #define FORKPTY_INCLUDE_H <pty.h>
    #endif
#endif
#include FORKPTY_INCLUDE_H

/* You probably don't need to alter these much, but if you do,
 * here is where you can define alternate character sets.
 *
 * Note that if your system's wide-character implementation
 * maps directly to Unicode, the preferred Unicode characters
 * will be used automatically if your system declares such
 * support. If it doesn't declare it, define WCHAR_IS_UNICODE to
 * force Unicode to be used.
 */
#define MAXMAP 0x7f
static wchar_t CSET_US[MAXMAP]; /* "USASCII"...really just the null table */

#if defined(__STDC_ISO_10646__) || defined(WCHAR_IS_UNICODE)
static wchar_t CSET_UK[MAXMAP] ={ /* "United Kingdom"...really just Pound Sterling */
    [L'#'] = 0x00a3
};

static wchar_t CSET_GRAPH[MAXMAP] ={ /* Graphics Set One */
    [L'-'] = 0x2191,
    [L'}'] = 0x00a3,
    [L'~'] = 0x00b7,
    [L'{'] = 0x03c0,
    [L','] = 0x2190,
    [L'+'] = 0x2192,
    [L'.'] = 0x2193,
    [L'|'] = 0x2260,
    [L'>'] = 0x2265,
    [L'`'] = 0x25c6,
    [L'a'] = 0x2592,
    [L'b'] = 0x2409,
    [L'c'] = 0x240c,
    [L'd'] = 0x240d,
    [L'e'] = 0x240a,
    [L'f'] = 0x00b0,
    [L'g'] = 0x00b1,
    [L'h'] = 0x2592,
    [L'i'] = 0x2603,
    [L'j'] = 0x2518,
    [L'k'] = 0x2510,
    [L'l'] = 0x250c,
    [L'm'] = 0x2514,
    [L'n'] = 0x253c,
    [L'o'] = 0x23ba,
    [L'p'] = 0x23bb,
    [L'q'] = 0x2500,
    [L'r'] = 0x23bc,
    [L's'] = 0x23bd,
    [L't'] = 0x251c,
    [L'u'] = 0x2524,
    [L'v'] = 0x2534,
    [L'w'] = 0x252c,
    [L'x'] = 0x2502,
    [L'y'] = 0x2264,
    [L'z'] = 0x2265,
    [L'_'] = L' ',
    [L'0'] = 0x25ae
};

#else /* wchar_t doesn't map to Unicode... */

static wchar_t CSET_UK[] ={ /* "United Kingdom"...really just Pound Sterling */
    [L'#'] = L'&'
};

static wchar_t CSET_GRAPH[] ={ /* Graphics Set One */
    [L'-'] = '^',
    [L'}'] = L'&',
    [L'~'] = L'o',
    [L'{'] = L'p',
    [L','] = L'<',
    [L'+'] = L'>',
    [L'.'] = L'v',
    [L'|'] = L'!',
    [L'>'] = L'>',
    [L'`'] = L'+',
    [L'a'] = L':',
    [L'b'] = L' ',
    [L'c'] = L' ',
    [L'd'] = L' ',
    [L'e'] = L' ',
    [L'f'] = L'\'',
    [L'g'] = L'#',
    [L'h'] = L'#',
    [L'i'] = L'i',
    [L'j'] = L'+',
    [L'k'] = L'+',
    [L'l'] = L'+',
    [L'm'] = L'+',
    [L'n'] = '+',
    [L'o'] = L'-',
    [L'p'] = L'-',
    [L'q'] = L'-',
    [L'r'] = L'-',
    [L's'] = L'_',
    [L't'] = L'+',
    [L'u'] = L'+',
    [L'v'] = L'+',
    [L'w'] = L'+',
    [L'x'] = L'|',
    [L'y'] = L'<',
    [L'z'] = L'>',
    [L'_'] = L' ',
    [L'0'] = L'#',
};

#endif
