/* Copyright 2017 Rob King <jking@deadpixi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <wctype.h>

#include "config.h"
#include "vtparser.h"

#define MAXCTABLE 72
#define MIN(x, y) ((x) < (y)? (x) : (y))
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define CTL(x) ((x) & 0x1f)
#define USAGE "usage: mtm [-t NAME] [-c KEY]"

/*** DATA TYPES */
typedef void (*PRINTER)(WINDOW *win, wchar_t w);

typedef enum{
    HORIZONTAL,
    VERTICAL,
    VIEW
} node_t;

typedef struct NODE NODE;
struct NODE{
    node_t t;
    NODE *p, *c1, *c2;
    int y, x, sy, sx, h, w, pt, vis, bot, top;
    short fg, bg, sfg, sbg, sp;
    bool insert, oxenl, xenl, decom, ckm, am, lnm, *tabs;
    PRINTER g0, g1, gc, gs;
    attr_t sattr;
    WINDOW *win;
    VTPARSER *vp;
};

typedef struct COLORTABLE COLORTABLE;
struct COLORTABLE{
    bool used;
    short f;
    short b;
};

/*** GLOBALS AND PROTOTYPES */
static COLORTABLE ctable[MAXCTABLE];
static NODE *root, *focused;
static bool cmd, kbs;
static int commandkey = CTL(COMMAND_KEY), nfds = 1; /* stdin */
static fd_set fds;
static char iobuf[BUFSIZ + 1];
static void reshape(NODE *n, int y, int x, int h, int w);
static void draw(NODE *n);
static void reshapechildren(NODE *n);
static const char *term = "eterm-color";
static void freenode(NODE *n, bool recursive);

/*** CHARACTER SETS
 * Each of the functions below takes a wide character and a window, and prints
 * that character to that window, optionally translating it first. We do this
 * by creating different PRINTER functions for each character set.
 *
 * We can't use static lookup tables because some of the special characters,
 * like the line-drawing ones, do not have constant representations.
 */

/* ASCII Character Set...really just "don't do any translation." */
static void
cset_ascii(WINDOW *win, wchar_t w)
{
    cchar_t r;
    attr_t a = A_NORMAL;
    short p = 0;
    wchar_t s[] = {w, 0};

    wattr_get(win, &a, &p, NULL);
    setcchar(&r, s, a, p, NULL);
    wadd_wchnstr(win, &r, 1);
}

/* UK Character Set */
static void
cset_uk(WINDOW *win, wchar_t w)
{
    w == L'#'? (void)wadd_wchnstr(win, WACS_STERLING, 1) : cset_ascii(win, w);
}

/* Graphics Character Set */
static void
cset_graphics(WINDOW *win, wchar_t w)
{
    switch (w){
        case L'}': wadd_wchnstr(win, WACS_STERLING, 1); return;
        case L'.': wadd_wchnstr(win, WACS_DARROW, 1);   return;
        case L',': wadd_wchnstr(win, WACS_LARROW, 1);   return;
        case L'+': wadd_wchnstr(win, WACS_RARROW, 1);   return;
        case L'-': wadd_wchnstr(win, WACS_UARROW, 1);   return;
        case L'h': wadd_wchnstr(win, WACS_BOARD, 1);    return;
        case L'~': wadd_wchnstr(win, WACS_BULLET, 1);   return;
        case L'a': wadd_wchnstr(win, WACS_CKBOARD, 1);  return;
        case L'f': wadd_wchnstr(win, WACS_DEGREE, 1);   return;
        case L'`': wadd_wchnstr(win, WACS_DIAMOND, 1);  return;
        case L'z': wadd_wchnstr(win, WACS_GEQUAL, 1);   return;
        case L'{': wadd_wchnstr(win, WACS_PI, 1);       return;
        case L'q': wadd_wchnstr(win, WACS_HLINE, 1);    return;
        case L'i': wadd_wchnstr(win, WACS_LANTERN, 1);  return;
        case L'n': wadd_wchnstr(win, WACS_PLUS, 1);     return;
        case L'y': wadd_wchnstr(win, WACS_LEQUAL, 1);   return;
        case L'm': wadd_wchnstr(win, WACS_LLCORNER, 1); return;
        case L'j': wadd_wchnstr(win, WACS_LRCORNER, 1); return;
        case L'|': wadd_wchnstr(win, WACS_NEQUAL, 1);   return;
        case L'g': wadd_wchnstr(win, WACS_PLMINUS, 1);  return;
        case L'o': wadd_wchnstr(win, WACS_S1, 1);       return;
        case L'p': wadd_wchnstr(win, WACS_S3, 1);       return;
        case L'r': wadd_wchnstr(win, WACS_S7, 1);       return;
        case L's': wadd_wchnstr(win, WACS_S9, 1);       return;
        case L'0': wadd_wchnstr(win, WACS_BLOCK, 1);    return;
        case L'w': wadd_wchnstr(win, WACS_TTEE, 1);     return;
        case L'u': wadd_wchnstr(win, WACS_RTEE, 1);     return;
        case L't': wadd_wchnstr(win, WACS_LTEE, 1);     return;
        case L'v': wadd_wchnstr(win, WACS_BTEE, 1);     return;
        case L'l': wadd_wchnstr(win, WACS_ULCORNER, 1); return;
        case L'k': wadd_wchnstr(win, WACS_URCORNER, 1); return;
        case L'x': wadd_wchnstr(win, WACS_VLINE, 1);    return;
        default:   cset_ascii(win, w);                  return;
    };
}

/*** UTILITY FUNCTIONS */
static void
quit(int rc, const char *m) /* Shut down MTM. */
{
    if (m)
        fprintf(stderr, "%s\n", m);
    freenode(root, true);
    endwin();
    exit(rc);
}

static void
safewrite(int fd, const char *b, size_t n) /* Write, checking for errors. */
{
    size_t w = 0;
    while (w < n){
        ssize_t s = write(fd, b + w, n - w);
        if (s < 0 && errno != EINTR)
            return;
        w += (size_t)s;
    }
}

static inline short
getpair(short f, short b) /* Get a curses color pair number given a fg/bg. */
{
    for (short i = 1; i < MAXCTABLE; i++){
        if (!ctable[i].used){
            ctable[i].used = true;
            ctable[i].f = f;
            ctable[i].b = b;
            init_pair(i, f, b);
        }
        if (ctable[i].f == f && ctable[i].b == b)
            return i;
    }

    return 0;
}

static const char *
getshell(void) /* Get the user's preferred shell. */
{
    if (getenv("SHELL"))
        return getenv("SHELL");

    struct passwd *pwd = getpwuid(getuid());
    if (pwd)
        return pwd->pw_shell;
    return "/bin/sh";
}

/*** TERMINAL EMULATION HANDLERS
 * These functions implement the various terminal commands activated by
 * escape sequences and printing to the terminal. Large amounts of boilerplate
 * code is shared among all these functions, and is factored out into the
 * macros below:
 *      PD(n, d)       - Parameter n, with default d.
 *      P0(n)          - Parameter n, default 0.
 *      P1(n)          - Parameter n, default 1.
 *      PW             - Parameter 0, as a wchar_t.
 *      SEND(n, s)     - Write string s to node n's host.
 *      (END)HANDLER   - Declare/end a handler function
 *      COMMONVARS     - All of the common variables for a handler.
 *                       x, y   - cursor position
 *                       mx, my - max possible values for x and y
 *                       n      - the current node
 *                       win    - the current window
 *
 * The funny names for handlers are from their ANSI/ECMA/DEC mnemonics.
 */
#define PD(x, d) (argc < (x) || !argv? (d) : argv[(x)])
#define P0(x) PD(x, 0)
#define P1(x) (!P0(x)? 1 : P0(x))
#define PW ((wchar_t)P0(0))
#define SEND(n, s) safewrite(n->pt, s, strlen(s))
#define COMMONVARS                                     \
    NODE *n = (NODE *)p;                               \
    WINDOW *win = n->win;                              \
    int y, x, my, mx;                                  \
    (void)v; (void)p; (void)w; (void)argc; (void)argv; \
    (void)win; (void)y; (void)x; (void)my; (void)mx;   \
    getyx(win, y, x);                                  \
    getmaxyx(win, my, mx);
#define HANDLER(name)                                           \
    static void                                                 \
    name (VTPARSER *v, void *p, wchar_t w, int argc, int *argv) \
    {                                                           \
        COMMONVARS;
#define ENDHANDLER                                              \
    }

HANDLER(bell) /* Terminal bell. */
    beep();
ENDHANDLER

HANDLER(cup) /* CUP - Cursor Position */
    n->xenl = false;
    wmove(win, (n->decom? n->top : 0) + P1(0) - 1, P1(1) - 1);
ENDHANDLER

HANDLER(dch) /* DCH - Delete Character */
    for (int i = 0; i < P1(0); i++)
        wdelch(win);
ENDHANDLER

HANDLER(ich) /* ICH - Insert Character */
    for (int i = 0; i < P1(0); i++)
        wins_nwstr(win, L" ", 1);
ENDHANDLER

HANDLER(cuu) /* CUU - Cursor Up */
    wmove(win, MAX(y - P1(0), n->top), x);
ENDHANDLER

HANDLER(cud) /* CUD - Cursor Down */
    wmove(win, MIN(y + P1(0), n->bot - 1), x);
ENDHANDLER

HANDLER(cuf) /* CUF - Cursor Forward */
    wmove(win, y, MIN(x + P1(0), mx - 1));
ENDHANDLER

HANDLER(ack) /* ACK - Acknowledge Enquiry */
    SEND(n, "\006");
ENDHANDLER

HANDLER(hts) /* HTS - Horizontal Tab Set */
    if (x < n->w && x > 0)
        n->tabs[x] = true;
ENDHANDLER

HANDLER(ri) /* RI - Reverse Index */
    y == n->top? wscrl(win, -1) : wmove(win, y - 1, x);
ENDHANDLER

HANDLER(sc) /* SC - Save Cursor */
    n->gs = n->gc;                           /* save current character set */
    n->sx = x;                               /* save X position            */
    n->sy = y;                               /* save Y position            */
    wattr_get(win, &n->sattr, &n->sp, NULL); /* save attrs and color pair  */
    n->sfg = n->fg;                          /* save foreground color      */
    n->sbg = n->bg;                          /* save background color      */
    n->oxenl = n->xenl;                      /* save xenl state            */
ENDHANDLER

HANDLER(rc) /* RC - Restore Cursor */
    n->gc = n->gs;                           /* get current character set */
    wmove(win, n->sy, n->sx);                /* get old position          */
    wattr_set(win, n->sattr, n->sp, NULL);   /* get attrs and color pair  */
    n->fg = n->sfg;                          /* get foreground color      */
    n->bg = n->sbg;                          /* get background color      */
    n->xenl = n->oxenl;                      /* get xenl state            */
ENDHANDLER

HANDLER(scs) /* SCS - Select Character Set */
    PRINTER *t = (PW == L'(')? &n->g0 : &n->g1;
    switch (w){
        case L'A': *t = cset_uk;       break;
        case L'B': *t = cset_ascii;    break;
        case L'0': *t = cset_graphics; break;
        case L'1': *t = cset_ascii;    break;
        case L'2': *t = cset_graphics; break;
    }
ENDHANDLER

HANDLER(tbc) /* TBC - Tabulation Clear */
    switch (P0(0)){
        case 0: n->tabs[x] = false;                            break;
        case 3: memset(n->tabs, 0, sizeof(bool) * (n->w - 1)); break;
    }
ENDHANDLER

HANDLER(cub) /* CUB - Cursor Backward */
    n->xenl = false;
    wmove(win, y, MAX(x - P1(0), 0));
ENDHANDLER

HANDLER(decaln) /* DECALN - DEC Alignment Test */
    chtype s[] = {COLOR_PAIR(0) | 'E', 0};
    for (int r = 0; r < my; r++){
        for (int c = 0; c <= mx; c++)
            mvwaddchnstr(win, r, c, s, 1);
    }
    wmove(win, y, x);
ENDHANDLER

HANDLER(decid) /* DECID - Identify Terminal */
    SEND(n, w == L'c'? "\033[?1;2c" : "\033[?6c");
ENDHANDLER

HANDLER(rcordecaln) /* RC or DECLN - Restore Cursor or DECALN */
    ((PW == L'#')? decaln : rc)(v, p, w, argc, argv);
ENDHANDLER

HANDLER(el) /* EL - Erase in Line */
    chtype s[] = {COLOR_PAIR(0) | ' ', 0};
    switch (P0(0)){
        case 0: wclrtoeol(win);                                         break;
        case 1: for (int i = 0; i <= x; i++) mvwaddchstr(win, y, i, s); break;
        case 2: wmove(win, y, 0); wclrtoeol(win);                       break;
    }
    wmove(win, y, x);
ENDHANDLER

HANDLER(ed) /* ED - Erase in Display */
    int o = 1;
    switch (P0(0)){
        case 0: wclrtobot(win);                   break;
        case 2: wmove(win, 0, 0); wclrtobot(win); break;
        case 1:
            for (int i = 0; i < y; i++){
                wmove(win, i, 0);
                wclrtoeol(win);
            }
            wmove(win, y, x);
            el(v, p, w, 1, &o);
            break;
    }
    wmove(win, y, x);
ENDHANDLER

HANDLER(decreqtparm) /* DECREQTPARM - Request Device Parameters */
    SEND(n, P0(0)? "\033[3;1;2;120;1;0x" : "\033[2;1;2;120;128;1;0x");
ENDHANDLER

HANDLER(dsr) /* DSR - Device Status Report */
    char buf[100] = {0};

    if (P0(0) == 5)
        strncpy(buf, "\033[0n", 99);
    else if (P0(0) == 6)
        snprintf(buf, 99, "\033[%d;%dR", (n->decom? y - n->top : y) + 1, x + 1);

    if (buf[0])
        SEND(n, buf);
ENDHANDLER

HANDLER(idl) /* IL or DL - Insert/Delete Line */
    /* Programs expect IL and DL to scroll as needed, so no insdelln... */
    wsetscrreg(win, y, n->bot - 1);
    wscrl(win, w == L'L'? -P1(0) : P1(0));
    wsetscrreg(win, n->top, n->bot - 1);
ENDHANDLER

HANDLER(csr) /* CSR - Change Scrolling Region */
    int t = P1(0) - 1;
    int b = PD(1, my);
    if (t < b && b <= my){
        n->top = t;
        n->bot = b;
        wsetscrreg(win, t, b - 1);
        cup(v, p, L'H', 0, NULL);
    }
ENDHANDLER

HANDLER(mode) /* Set or Reset Mode */
    bool set = (w == L'h');
    for (int i = 0; i < argc; i++) switch (P0(i)){
        case  1: n->ckm = set;                             break;
        case  3: werase(win); wmove(win, 0, 0);            break;
        case  4: n->insert = set;                          break;
        case  6: n->decom = set; cup(v, p, L'H', 0, NULL); break;
        case  7: n->am = set;                              break;
        case 20: n->lnm = set;                             break;
        case 25: n->vis = set;                             break;
    }
ENDHANDLER

HANDLER(sgr0) /* Reset SGR to default */
    wattrset(n->win, A_NORMAL);
    wcolor_set(n->win, 0, NULL);
    n->fg = n->bg = -1;
ENDHANDLER

HANDLER(ris) /* RIS - Reset to Initial State */
    sgr0(v, p, 0, 0, NULL);
    wclear(win);
    wmove(win, 0, 0);
    n->insert = n->oxenl = n->xenl = n->decom = n->lnm = false;
    n->ckm = n->am = true;
    n->top = 0;
    n->bot = n->h;
    wsetscrreg(win, 0, n->h - 1);
    for (int i = 0; i < mx; i++)
        n->tabs[i] = (i % 8 == 0);
ENDHANDLER

HANDLER(sgr) /* SGR - Select Graphic Rendition */
    bool doc = false;

    if (!argc)
        sgr0(v, p, 0, 0, NULL);

    for (int i = 0; i < argc; i++) switch (P0(i)){
        case  0: sgr0(v, p, 0, 0, NULL);              break;
        case  1: wattron(win,  A_BOLD);               break;
        case  4: wattron(win,  A_UNDERLINE);          break;
        case  5: wattron(win,  A_BLINK);              break;
        case  7: wattron(win,  A_REVERSE);            break;
        case  8: wattron(win,  A_INVIS);              break;
        case 24: wattroff(win, A_UNDERLINE);          break;
        case 27: wattroff(win, A_REVERSE);            break;
        case 30: n->fg = COLOR_BLACK;   doc = true;   break;
        case 31: n->fg = COLOR_RED;     doc = true;   break;
        case 32: n->fg = COLOR_GREEN;   doc = true;   break;
        case 33: n->fg = COLOR_YELLOW;  doc = true;   break;
        case 34: n->fg = COLOR_BLUE;    doc = true;   break;
        case 35: n->fg = COLOR_MAGENTA; doc = true;   break;
        case 36: n->fg = COLOR_CYAN;    doc = true;   break;
        case 37: n->fg = COLOR_WHITE;   doc = true;   break;
        case 39: n->fg = -1;            doc = true;   break;
        case 40: n->bg = COLOR_BLACK;   doc = true;   break;
        case 41: n->bg = COLOR_RED;     doc = true;   break;
        case 42: n->bg = COLOR_GREEN;   doc = true;   break;
        case 43: n->bg = COLOR_YELLOW;  doc = true;   break;
        case 44: n->bg = COLOR_BLUE;    doc = true;   break;
        case 45: n->bg = COLOR_MAGENTA; doc = true;   break;
        case 46: n->bg = COLOR_CYAN;    doc = true;   break;
        case 47: n->bg = COLOR_WHITE;   doc = true;   break;
        case 49: n->bg = -1;            doc = true;   break;
    }

    if (doc)
        wcolor_set(win, getpair(n->fg, n->bg), NULL);
}

HANDLER(ht) /* HT - Horizontal Tab */
    for (int i = x + 1; i < n->w; i++) if (n->tabs[i]){
        wmove(win, y, i);
        return;
    }
    wmove(win, y, mx - 1);
ENDHANDLER

HANDLER(cr) /* CR - Carriage Return */
    n->xenl = false;
    wmove(win, y, 0);
ENDHANDLER

HANDLER(ind) /* IND - Index */
    y == n->bot - 1? scroll(win) : wmove(win, y + 1, x);
ENDHANDLER

HANDLER(nel) /* NEL - Next Line */
    cr(v, p, w, 0, NULL);
    ind(v, p, w, 0, NULL);
ENDHANDLER

HANDLER(pnl) /* NL - Newline */
    ind(v, p, w, 0, NULL);
    if (n->lnm)
        cr(v, p, w, 0, NULL);
ENDHANDLER

HANDLER(so) /* SO/SI - Switch Out/In character set */
    n->gc = (w == 0x0e)? n->g1 : n->g0;
ENDHANDLER

HANDLER(print) /* Print a character to the terminal */
    if (n->insert)
        ich(v, p, L'@', 0, NULL);

    if (n->xenl){
        n->xenl = false;
        if (n->am)
            nel(v, p, L'\n', 0, NULL);
        getyx(win, y, x);
    }

    n->gc(n->win, w);
    if (wmove(win, y, x + 1) == ERR)
        n->xenl = true;

    wnoutrefresh(win);
ENDHANDLER

static void
setupevents(NODE *n) /* Wire up escape sequences to functions. */
{
    vtparser_onevent(n->vp, VTPARSER_CONTROL, 0x05, ack);
    vtparser_onevent(n->vp, VTPARSER_CONTROL, 0x07, bell);
    vtparser_onevent(n->vp, VTPARSER_CONTROL, 0x08, cub);
    vtparser_onevent(n->vp, VTPARSER_CONTROL, 0x09, ht);
    vtparser_onevent(n->vp, VTPARSER_CONTROL, 0x0a, pnl);
    vtparser_onevent(n->vp, VTPARSER_CONTROL, 0x0b, pnl);
    vtparser_onevent(n->vp, VTPARSER_CONTROL, 0x0c, pnl);
    vtparser_onevent(n->vp, VTPARSER_CONTROL, 0x0d, cr);
    vtparser_onevent(n->vp, VTPARSER_CONTROL, 0x0e, so);
    vtparser_onevent(n->vp, VTPARSER_CONTROL, 0x0f, so);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'A', cuu);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'B', cud);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'C', cuf);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'D', cub);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'H', cup);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'J', ed);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'K', el);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'L', idl);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'M', idl);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'P', dch);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'@', ich);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'c', decid);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'f', cup);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'g', tbc);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'h', mode);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'l', mode);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'm', sgr);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'n', dsr);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'r', csr);
    vtparser_onevent(n->vp, VTPARSER_CSI,     L'x', decreqtparm);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'0', scs);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'1', scs);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'2', scs);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'7', sc);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'8', rcordecaln);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'A', scs);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'B', scs);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'D', ind);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'E', nel);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'H', hts);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'M', ri);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'Z', decid);
    vtparser_onevent(n->vp, VTPARSER_ESCAPE,  L'c', ris);
    vtparser_onevent(n->vp, VTPARSER_PRINT,   0,    print);
}

/*** MTM FUNCTIONS
 * These functions do the user-visible work of MTM: creating nodes in the
 * tree, updating the display, and so on.
 */
static bool *
newtabs(int w, int ow, bool *oldtabs) /* Initialize default tabstops. */
{
    bool *tabs = calloc(w, sizeof(bool));
    if (!tabs)
        return NULL;

    for (int i = 0; i < w; i++)
        tabs[i] = i < ow? oldtabs[i] : (i % 8 == 0);

    return tabs;
}

static NODE *
newnode(node_t t, NODE *p, int y, int x, int h, int w) /* Create a new node. */
{
    NODE *n = calloc(1, sizeof(NODE));
    bool *tabs = newtabs(w, 0, NULL);
    if (!n || !tabs || h < 2 || w < 2)
        return free(n), free(tabs), NULL;

    n->gc = n->g0 = cset_ascii;
    n->g1 = cset_graphics;
    n->t = t;
    n->pt = -1;
    n->p = p;
    n->y = y;
    n->x = x;
    n->h = h;
    n->w = w;
    n->tabs = tabs;

    return n;
}

static void
freenode(NODE *n, bool recurse) /* Free a node. */
{
    if (n){
        if (n->win)
            delwin(n->win);
        if (n->vp)
            vtparser_close(n->vp);
        if (recurse)
            freenode(n->c1, true);
        if (recurse)
            freenode(n->c2, true);
        if (n->pt >= 0){
            close(n->pt);
            FD_CLR(n->pt, &fds);
        }
        free(n->tabs);
        free(n);
    }
}

static void
fixcursor(void) /* Move the terminal cursor to the active view. */
{
    if (focused){
        int y, x;
        getyx(focused->win, y, x);
        wmove(focused->win, y, x);
        curs_set(focused->vis);
        wrefresh(focused->win);
    }
}

static NODE *
newview(NODE *p, int y, int x, int h, int w) /* Open a new view. */
{
    struct winsize ws = {.ws_row = h, .ws_col = w};
    NODE *n = newnode(VIEW, p, y, x, h, w);
    if (!n)
        return NULL;

    n->fg = n->bg = -1;
    n->vis = 1;
    n->top = 0;
    n->bot = h;
    n->am = n->ckm = true;
    n->win = newwin(h, w, y, x);
    nodelay(n->win, TRUE);
    scrollok(n->win, TRUE);
    keypad(n->win, TRUE);
    if (!n->win)
        return freenode(n, false), NULL;

    n->vp = vtparser_open(n);
    if (!n)
        return freenode(n, false), NULL;
    setupevents(n);

    pid_t pid = forkpty(&n->pt, NULL, NULL, &ws);
    if (pid < 0)
        return freenode(n, false), NULL;
    else if (pid == 0){
        setsid();
        setenv("TERM", term, 1);
        signal(SIGCHLD, SIG_DFL);
        execl(getshell(), getshell(), NULL);
        return NULL;
    }

    FD_SET(n->pt, &fds);
    fcntl(n->pt, F_SETFL, O_NONBLOCK);
    nfds = n->pt > nfds? n->pt : nfds;
    return n;
}

static NODE *
newcontainer(node_t t, NODE *p, int y, int x, int h, int w,
             NODE *c1, NODE *c2) /* Create a new container */
{
    NODE *n = newnode(t, p, y, x, h, w);
    if (!n)
        return NULL;

    n->c1 = c1;
    n->c2 = c2;
    c1->p = c2->p = n;

    reshapechildren(n);
    return n;
}

static void
focus(NODE *n) /* Focus a node. */
{
    if (!n)
        return;
    else if (n->t == VIEW){
        focused = n;
        wnoutrefresh(n->win);
    } else
        focus(n->c1? n->c1 : n->c2);
}

#define ABOVE(n) n->y - 2, n->x + n->w / 2
#define BELOW(n) n->y + n->h + 2, n->x + n->w / 2
#define LEFT(n)  n->y + n->h / 2, n->x - 2
#define RIGHT(n) n->y + n->h / 2, n->x + n->w + 2
#define IN(n, y, x) (y >= n->y && y <= n->y + n->h && \
                     x >= n->x && x <= n->x + n->w)

static NODE *
findnode(NODE *n, int y, int x) /* Find the node enclosing y,x. */
{
    if (IN(n, y, x)){
        if (n->c1 && IN(n->c1, y, x))
            return findnode(n->c1, y, x);
        if (n->c2 && IN(n->c2, y, x))
            return findnode(n->c2, y, x);
        return n;
    }
    return NULL;
}

static void
replacechild(NODE *n, NODE *c1, NODE *c2) /* Replace c1 of n with c2. */
{
    c2->p = n;
    if (!n){
        root = c2;
        reshape(c2, 0, 0, LINES, COLS);
    } else if (n->c1 == c1)
        n->c1 = c2;
    else if (n->c2 == c1)
        n->c2 = c2;

    n = n? n : root;
    reshape(n, n->y, n->x, n->h, n->w);
    draw(n);
}

static void
removechild(NODE *p, const NODE *c) /* Replace p with other child. */
{
    replacechild(p->p, p, c == p->c1? p->c2 : p->c1);
    freenode(p, false);
}

static void
deletenode(NODE *n) /* Delete a node. */
{
    if (!n || !n->p)
        quit(EXIT_SUCCESS, NULL);
    if (n == focused)
        focus(n->p->c1 == n? n->p->c2 : n->p->c1);

    removechild(n->p, n);
    freenode(n, true);
}

static void
reshapeview(NODE *n, int y, int x, int h, int w) /* Reshape a view. */
{
    struct winsize ws = {.ws_row = h, .ws_col = w};
    bool *tabs = newtabs(w, n->w, n->tabs);
    if (!tabs)
        return;

    free(n->tabs);
    n->tabs = tabs;
    mvwin(n->win, 0, 0);
    wresize(n->win, h? h : 1, w? w : 1);
    mvwin(n->win, y, x);
    wnoutrefresh(n->win);
    csr(n->vp, n, L'r', 0, NULL);
    ioctl(n->pt, TIOCSWINSZ, &ws);
}

static void
reshapechildren(NODE *n) /* Reshape all children of a view. */
{
    if (n->t == HORIZONTAL){
        reshape(n->c1, n->y, n->x, n->h, n->w / 2);
        reshape(n->c2, n->y, n->x + n->w / 2 + 1, n->h, n->w / 2 - 1);
    } else if (n->t == VERTICAL){
        reshape(n->c1, n->y, n->x, n->h / 2, n->w);
        reshape(n->c2, n->y + n->h / 2 + 1, n->x, n->h / 2 - 1, n->w);
    }
}

static void
reshape(NODE *n, int y, int x, int h, int w) /* Reshape a node. */
{
    n->y = y;
    n->x = x;
    n->h = h? h : 1;
    n->w = w? w : 1;

    if (n->t == VIEW)
        reshapeview(n, y, x, h, w);
    else
        reshapechildren(n);
    draw(n);
    wnoutrefresh(n->win);
}

static void
drawchildren(const NODE *n) /* Draw all children of n. */
{
    draw(n->c1);
    if (n->t == HORIZONTAL)
        mvvline(n->y, n->x + n->w / 2, ACS_VLINE, n->h);
    else
        mvhline(n->y + n->h / 2, n->x, ACS_HLINE, n->w);
    wnoutrefresh(stdscr);
    draw(n->c2);
}

static void
draw(NODE *n) /* Draw a node. */
{
    if (n->t == VIEW)
        wnoutrefresh(n->win);
    else
        drawchildren(n);
}

static void
split(NODE *n, node_t t) /* Split a node. */
{
    int nh = t == VERTICAL?   (n->h - 1) / 2 : n->h;
    int nw = t == HORIZONTAL? (n->w) / 2 : n->w;
    NODE *p = n->p;
    NODE *v = newview(NULL, 0, 0, MAX(0, nh), MAX(0, nw));
    if (!v)
        return;

    /* wclear(n->win);*/ wrefresh(n->win);
    NODE *c = newcontainer(t, n->p, n->y, n->x, n->h, n->w, n, v);
    if (!c){
        freenode(v, false);
        return;
    }

    replacechild(p, n, c);
    focus(v);
    draw(p? p : root);
}

static bool
getinput(NODE *n, fd_set *f) /* Recursively check all ptty's for input. */
{
    if (n && n->c1 && !getinput(n->c1, f))
        return false;
    if (n && n->c2 && !getinput(n->c2, f))
        return false;
    if (n && n->pt >= 0 && FD_ISSET(n->pt, f)){
        ssize_t r = read(n->pt, iobuf, BUFSIZ);
        if (r > 0)
            vtparser_write(n->vp, iobuf, r);
        else if (r < 0 && errno != EINTR && errno != EWOULDBLOCK)
            return deletenode(n), false;
    }
    return true;
}

static bool
handlechar(int k) /* Handle a single input character. */
{
    #define DO(s, i, a) if (s == cmd && i == k) { a ; cmd = false; return true;}
    DO(cmd,   ERR,           return false)
    DO(cmd,   KEY_RESIZE,    reshape(root, 0, 0, LINES, COLS))
    DO(cmd,   KEY_BACKSPACE, SEND(focused, kbs? "\010" : "\177"))
    DO(cmd,   KEY_DC,        SEND(focused, "\177"))
    DO(false, '\n',          SEND(focused, focused->lnm? "\r\n" : "\r"))
    DO(false, KEY_UP,        SEND(focused, focused->ckm? "\033OA" : "\033[A"))
    DO(false, KEY_DOWN,      SEND(focused, focused->ckm? "\033OB" : "\033[B"))
    DO(false, KEY_RIGHT,     SEND(focused, focused->ckm? "\033OC" : "\033[C"))
    DO(false, KEY_LEFT,      SEND(focused, focused->ckm? "\033OD" : "\033[D"))
    DO(false, KEY_HOME,      SEND(focused, "\033[1~"))
    DO(false, KEY_END,       SEND(focused, "\033[4~"))
    DO(false, KEY_PPAGE,     SEND(focused, "\033[5~"))
    DO(false, KEY_NPAGE,     SEND(focused, "\033[6~"))
    DO(false, commandkey,    return cmd = true)
    DO(true,  MOVE_UP,       focus(findnode(root, ABOVE(focused))))
    DO(true,  MOVE_DOWN,     focus(findnode(root, BELOW(focused))))
    DO(true,  MOVE_LEFT,     focus(findnode(root, LEFT(focused))))
    DO(true,  MOVE_RIGHT,    focus(findnode(root, RIGHT(focused))))
    DO(true,  HSPLIT,        split(focused, HORIZONTAL))
    DO(true,  VSPLIT,        split(focused, VERTICAL))
    DO(true,  DELETE_NODE,   deletenode(focused))
    DO(true,  REDRAW,        draw(root))
    char c[] = {(char)k, 0};
    SEND(focused, c);
    return cmd = false, true;
}

static void
run(void) /* Run MTM. */
{
    while (root){
        fd_set sfds = fds;
        if (select(nfds + 1, &sfds, NULL, NULL, NULL) < 0)
            FD_ZERO(&sfds);
        while (handlechar(wgetch(focused->win))) ;
        getinput(root, &sfds);
        doupdate();
        fixcursor();
    }
}

int
main(int argc, char **argv)
{
    FD_SET(STDIN_FILENO, &fds);
    setlocale(LC_ALL, "");
    signal(SIGCHLD, SIG_IGN);
    int c = 0;
    while ((c = getopt(argc, argv, "mubt:c:")) != -1) switch (c){
        case 'b': kbs = true;                  break;
        case 'c': commandkey = CTL(optarg[0]); break;
        case 't': term = optarg;               break;
        case 'm': /* ignored */                break;
        case 'u': /* ignored */                break;
        default:  quit(EXIT_FAILURE, USAGE);   break;
    }

    initscr();
    raw();
    noecho();
    intrflush(stdscr, FALSE);
    start_color();
    use_default_colors();
    focus(root = newview(NULL, 0, 0, LINES, COLS));
    draw(root);
    run();

    quit(EXIT_SUCCESS, NULL);
    return EXIT_SUCCESS; /* not reached */
}
