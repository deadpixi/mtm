/* Copyright 2017 - 2019 Rob King <jking@deadpixi.com>
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
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "vtparser.h"

/*** CONFIGURATION */
#include "config.h"

#define MIN(x, y) ((x) < (y)? (x) : (y))
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define CTL(x) ((x) & 0x1f)
#define USAGE "usage: mtm [-T NAME] [-t NAME] [-c KEY]\n"

/*** DATA TYPES */
typedef enum{
    HORIZONTAL,
    VERTICAL,
    VIEW
} Node;

typedef struct SCRN SCRN;
struct SCRN{
    int sy, sx, vis, tos, off;
    short fg, bg, sfg, sbg, sp;
    bool insert, oxenl, xenl, saved;
    attr_t sattr;
    WINDOW *win;
};

typedef struct NODE NODE;
struct NODE{
    Node t;
    int y, x, h, w, pt, ntabs;
    bool *tabs, pnm, decom, am, lnm;
    wchar_t repc;
    NODE *p, *c1, *c2;
    SCRN pri, alt, *s;
    wchar_t *g0, *g1, *g2, *g3, *gc, *gs, *sgc, *sgs;
    VTPARSER vp;
};

/*** GLOBALS AND PROTOTYPES */
static NODE *root, *focused, *lastfocused = NULL;
static int commandkey = CTL(COMMAND_KEY), nfds = 1; /* stdin */
static fd_set fds;
static char iobuf[BUFSIZ];

static void setupevents(NODE *n);
static void reshape(NODE *n, int y, int x, int h, int w);
static void draw(NODE *n);
static void reshapechildren(NODE *n);
static const char *term = NULL;
static void freenode(NODE *n, bool recursive);
void start_pairs(void);
short mtm_alloc_pair(int fg, int bg);

/*** UTILITY FUNCTIONS */
static void
quit(int rc, const char *m) /* Shut down MTM. */
{
    if (m)
        fprintf(stderr, "%s\n", m);
    if (root)
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
        else if (s < 0)
            s = 0;
        w += (size_t)s;
    }
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
 *      CALL(h)        - Call handler h with no arguments.
 *      SENDN(n, s, c) - Write string c bytes of s to n.
 *      SEND(n, s)     - Write string s to node n's host.
 *      (END)HANDLER   - Declare/end a handler function
 *      COMMONVARS     - All of the common variables for a handler.
 *                       x, y     - cursor position
 *                       mx, my   - max possible values for x and y
 *                       px, py   - physical cursor position in scrollback
 *                       n        - the current node
 *                       win      - the current window
 *                       top, bot - the scrolling region
 *                       tos      - top of the screen in the pad
 *                       s        - the current SCRN buffer
 * The funny names for handlers are from their ANSI/ECMA/DEC mnemonics.
 */
#define PD(x, d) (argc < (x) || !argv? (d) : argv[(x)])
#define P0(x) PD(x, 0)
#define P1(x) (!P0(x)? 1 : P0(x))
#define CALL(x) (x)(v, n, 0, 0, 0, NULL, NULL)
#define SENDN(n, s, c) safewrite(n->pt, s, c)
#define SEND(n, s) SENDN(n, s, strlen(s))
#define COMMONVARS                                                      \
    NODE *n = (NODE *)p;                                                \
    SCRN *s = n->s;                                                     \
    WINDOW *win = s->win;                                               \
    int py, px, y, x, my, mx, top = 0, bot = 0, tos = s->tos;           \
    (void)v; (void)p; (void)w; (void)iw; (void)argc; (void)argv;        \
    (void)win; (void)y; (void)x; (void)my; (void)mx; (void)osc;         \
    (void)tos;                                                          \
    getyx(win, py, px); y = py - s->tos; x = px;                        \
    getmaxyx(win, my, mx); my -= s->tos;                                \
    wgetscrreg(win, &top, &bot);                                        \
    bot++; bot -= s->tos;                                               \
    top = top <= tos? 0 : top - tos;                                    \

#define HANDLER(name)                                   \
    static void                                         \
    name (VTPARSER *v, void *p, wchar_t w, wchar_t iw,  \
          int argc, int *argv, const wchar_t *osc)      \
    { COMMONVARS
#define ENDHANDLER n->repc = 0; } /* control sequences aren't repeated */

HANDLER(bell) /* Terminal bell. */
    beep();
ENDHANDLER

HANDLER(numkp) /* Application/Numeric Keypad Mode */
    n->pnm = (w == L'=');
ENDHANDLER

HANDLER(vis) /* Cursor visibility */
    s->vis = iw == L'6'? 0 : 1;
ENDHANDLER

HANDLER(cup) /* CUP - Cursor Position */
    s->xenl = false;
    wmove(win, tos + (n->decom? top : 0) + P1(0) - 1, P1(1) - 1);
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
    wmove(win, MAX(py - P1(0), tos + top), x);
ENDHANDLER

HANDLER(cud) /* CUD - Cursor Down */
    wmove(win, MIN(py + P1(0), tos + bot - 1), x);
ENDHANDLER

HANDLER(cuf) /* CUF - Cursor Forward */
    wmove(win, py, MIN(x + P1(0), mx - 1));
ENDHANDLER

HANDLER(ack) /* ACK - Acknowledge Enquiry */
    SEND(n, "\006");
ENDHANDLER

HANDLER(hts) /* HTS - Horizontal Tab Set */
    if (x < n->ntabs && x > 0)
        n->tabs[x] = true;
ENDHANDLER

HANDLER(ri) /* RI - Reverse Index */
    int otop = 0, obot = 0;
    wgetscrreg(win, &otop, &obot);
    wsetscrreg(win, otop >= tos? otop : tos, obot);
    y == top? wscrl(win, -1) : wmove(win, MAX(tos, py - 1), x);
    wsetscrreg(win, otop, obot);
ENDHANDLER

HANDLER(decid) /* DECID - Send Terminal Identification */
    if (w == L'c')
        SEND(n, iw == L'>'? "\033[>1;10;0c" : "\033[?1;2c");
    else if (w == L'Z')
        SEND(n, "\033[?6c");
ENDHANDLER

HANDLER(hpa) /* HPA - Cursor Horizontal Absolute */
    wmove(win, py, MIN(P1(0) - 1, mx - 1));
ENDHANDLER

HANDLER(hpr) /* HPR - Cursor Horizontal Relative */
    wmove(win, py, MIN(px + P1(0), mx - 1));
ENDHANDLER

HANDLER(vpa) /* VPA - Cursor Vertical Absolute */
    wmove(win, MIN(tos + bot - 1, MAX(tos + top, tos + P1(0) - 1)), x);
ENDHANDLER

HANDLER(vpr) /* VPR - Cursor Vertical Relative */
    wmove(win, MIN(tos + bot - 1, MAX(tos + top, py + P1(0))), x);
ENDHANDLER

HANDLER(cbt) /* CBT - Cursor Backwards Tab */
    for (int i = x - 1; i < n->ntabs && i >= 0; i--) if (n->tabs[i]){
        wmove(win, py, i);
        return;
    }
    wmove(win, py, 0);
ENDHANDLER

HANDLER(ht) /* HT - Horizontal Tab */
    for (int i = x + 1; i < n->w && i < n->ntabs; i++) if (n->tabs[i]){
        wmove(win, py, i);
        return;
    }
    wmove(win, py, mx - 1);
ENDHANDLER

HANDLER(tab) /* Tab forwards or backwards */
    for (int i = 0; i < P1(0); i++) switch (w){
        case L'I':  CALL(ht);  break;
        case L'\t': CALL(ht);  break;
        case L'Z':  CALL(cbt); break;
    }
ENDHANDLER

HANDLER(decaln) /* DECALN - Screen Alignment Test */
    chtype e[] = {COLOR_PAIR(0) | 'E', 0};
    for (int r = 0; r < my; r++){
        for (int c = 0; c <= mx; c++)
            mvwaddchnstr(win, tos + r, c, e, 1);
    }
    wmove(win, py, px);
ENDHANDLER

HANDLER(su) /* SU - Scroll Up/Down */
    wscrl(win, (w == L'T' || w == L'^')? -P1(0) : P1(0));
ENDHANDLER

HANDLER(sc) /* SC - Save Cursor */
    s->sx = px;                              /* save X position            */
    s->sy = py;                              /* save Y position            */
    wattr_get(win, &s->sattr, &s->sp, NULL); /* save attrs and color pair  */
    s->sfg = s->fg;                          /* save foreground color      */
    s->sbg = s->bg;                          /* save background color      */
    s->oxenl = s->xenl;                      /* save xenl state            */
    s->saved = true;                         /* save data is valid         */
    n->sgc = n->gc; n->sgs = n->gs;          /* save character sets        */
ENDHANDLER

HANDLER(rc) /* RC - Restore Cursor */
    if (iw == L'#'){
        CALL(decaln);
        return;
    }
    if (!s->saved)
        return;
    wmove(win, s->sy, s->sx);                /* get old position          */
    wattr_set(win, s->sattr, s->sp, NULL);   /* get attrs and color pair  */
    s->fg = s->sfg;                          /* get foreground color      */
    s->bg = s->sbg;                          /* get background color      */
    s->xenl = s->oxenl;                      /* get xenl state            */
    n->gc = n->sgc; n->gs = n->sgs;          /* save character sets        */

    /* restore colors */
    int cp = mtm_alloc_pair(s->fg, s->bg);
    wcolor_set(win, cp, NULL);
    cchar_t c;
    setcchar(&c, L" ", A_NORMAL, cp, NULL);
    wbkgrndset(win, &c);
ENDHANDLER

HANDLER(tbc) /* TBC - Tabulation Clear */
    switch (P0(0)){
        case 0: n->tabs[x < n->ntabs? x : 0] = false;          break;
        case 3: memset(n->tabs, 0, sizeof(bool) * (n->ntabs)); break;
    }
ENDHANDLER

HANDLER(cub) /* CUB - Cursor Backward */
    s->xenl = false;
    wmove(win, py, MAX(x - P1(0), 0));
ENDHANDLER

HANDLER(el) /* EL - Erase in Line */
    cchar_t b;
    setcchar(&b, L" ", A_NORMAL, mtm_alloc_pair(s->fg, s->bg), NULL);
    switch (P0(0)){
        case 0: wclrtoeol(win);                                                 break;
        case 1: for (int i = 0; i <= x; i++) mvwadd_wchnstr(win, py, i, &b, 1); break;
        case 2: wmove(win, py, 0); wclrtoeol(win);                              break;
    }
    wmove(win, py, x);
ENDHANDLER

HANDLER(ed) /* ED - Erase in Display */
    int o = 1;
    switch (P0(0)){
        case 0: wclrtobot(win);                     break;
        case 3: werase(win);                        break;
        case 2: wmove(win, tos, 0); wclrtobot(win); break;
        case 1:
            for (int i = tos; i < py; i++){
                wmove(win, i, 0);
                wclrtoeol(win);
            }
            wmove(win, py, x);
            el(v, p, w, iw, 1, &o, NULL);
            break;
    }
    wmove(win, py, px);
ENDHANDLER

HANDLER(ech) /* ECH - Erase Character */
    cchar_t c;
    setcchar(&c, L" ", A_NORMAL, mtm_alloc_pair(s->fg, s->bg), NULL);
    for (int i = 0; i < P1(0); i++)
        mvwadd_wchnstr(win, py, x + i, &c, 1);
    wmove(win, py, px);
ENDHANDLER

HANDLER(dsr) /* DSR - Device Status Report */
    char buf[100] = {0};
    if (P0(0) == 6)
        snprintf(buf, sizeof(buf) - 1, "\033[%d;%dR",
                 (n->decom? y - top : y) + 1, x + 1);
    else
        snprintf(buf, sizeof(buf) - 1, "\033[0n");
    SEND(n, buf);
ENDHANDLER

HANDLER(idl) /* IL or DL - Insert/Delete Line */
    /* we don't use insdelln here because it inserts above and not below,
     * and has a few other edge cases... */
    int otop = 0, obot = 0, p1 = MIN(P1(0), (my - 1) - y);
    wgetscrreg(win, &otop, &obot);
    wsetscrreg(win, py, obot);
    wscrl(win, w == L'L'? -p1 : p1);
    wsetscrreg(win, otop, obot);
    wmove(win, py, 0);
ENDHANDLER

HANDLER(csr) /* CSR - Change Scrolling Region */
    if (wsetscrreg(win, tos + P1(0) - 1, tos + PD(1, my) - 1) == OK)
        CALL(cup);
ENDHANDLER

HANDLER(decreqtparm) /* DECREQTPARM - Request Device Parameters */
    SEND(n, P0(0)? "\033[3;1;2;120;1;0x" : "\033[2;1;2;120;128;1;0x");
ENDHANDLER

HANDLER(sgr0) /* Reset SGR to default */
    wattrset(win, A_NORMAL);
    wcolor_set(win, 0, NULL);
    s->fg = s->bg = -1;
    wbkgdset(win, COLOR_PAIR(0) | ' ');
ENDHANDLER

HANDLER(cls) /* Clear screen */
    CALL(cup);
    wclrtobot(win);
    CALL(cup);
ENDHANDLER

HANDLER(ris) /* RIS - Reset to Initial State */
    n->gs = n->gc = n->g0 = CSET_US; n->g1 = CSET_GRAPH;
    n->g2 = CSET_US; n->g3 = CSET_GRAPH;
    n->decom = s->insert = s->oxenl = s->xenl = n->lnm = false;
    CALL(cls);
    CALL(sgr0);
    n->am = true;
    n->pnm = false;
    n->pri.vis = n->alt.vis = 1;
    n->s = &n->pri;
    wsetscrreg(n->pri.win, 0, MAX(SCROLLBACK, n->h) - 1);
    wsetscrreg(n->alt.win, 0, n->h - 1);
    for (int i = 0; i < n->ntabs; i++)
        n->tabs[i] = (i % 8 == 0);
ENDHANDLER

HANDLER(mode) /* Set or Reset Mode */
    bool set = (w == L'h');
    for (int i = 0; i < argc; i++) switch (P0(i)){
        case  1: n->pnm = set;              break;
        case  3: CALL(cls);                 break;
        case  4: s->insert = set;           break;
        case  6: n->decom = set; CALL(cup); break;
        case  7: n->am = set;               break;
        case 20: n->lnm = set;              break;
        case 25: s->vis = set? 1 : 0;       break;
        case 34: s->vis = set? 1 : 2;       break;
        case 1048: CALL((set? sc : rc));    break;
        case 1049:
            CALL((set? sc : rc)); /* fall-through */
        case 47: case 1047: if (set && n->s != &n->alt){
                n->s = &n->alt;
                CALL(cls);
            } else if (!set && n->s != &n->pri)
                n->s = &n->pri;
            break;
    }
ENDHANDLER

HANDLER(sgr) /* SGR - Select Graphic Rendition */
    bool doc = false, do8 = COLORS >= 8, do16 = COLORS >= 16, do256 = COLORS >= 256;
    if (!argc)
        CALL(sgr0);

    short bg = s->bg, fg = s->fg;
    for (int i = 0; i < argc; i++) switch (P0(i)){
        case  0:  CALL(sgr0);                                              break;
        case  1:  wattron(win,  A_BOLD);                                   break;
        case  2:  wattron(win,  A_DIM);                                    break;
        case  4:  wattron(win,  A_UNDERLINE);                              break;
        case  5:  wattron(win,  A_BLINK);                                  break;
        case  7:  wattron(win,  A_REVERSE);                                break;
        case  8:  wattron(win,  A_INVIS);                                  break;
        case 22:  wattroff(win, A_DIM); wattroff(win, A_BOLD);             break;
        case 24:  wattroff(win, A_UNDERLINE);                              break;
        case 25:  wattroff(win, A_BLINK);                                  break;
        case 27:  wattroff(win, A_REVERSE);                                break;
        case 30:  fg = COLOR_BLACK;                           doc = do8;   break;
        case 31:  fg = COLOR_RED;                             doc = do8;   break;
        case 32:  fg = COLOR_GREEN;                           doc = do8;   break;
        case 33:  fg = COLOR_YELLOW;                          doc = do8;   break;
        case 34:  fg = COLOR_BLUE;                            doc = do8;   break;
        case 35:  fg = COLOR_MAGENTA;                         doc = do8;   break;
        case 36:  fg = COLOR_CYAN;                            doc = do8;   break;
        case 37:  fg = COLOR_WHITE;                           doc = do8;   break;
        case 38:  fg = P0(i+1) == 5? P0(i+2) : s->fg; i += 2; doc = do256; break;
        case 39:  fg = -1;                                    doc = true;  break;
        case 40:  bg = COLOR_BLACK;                           doc = do8;   break;
        case 41:  bg = COLOR_RED;                             doc = do8;   break;
        case 42:  bg = COLOR_GREEN;                           doc = do8;   break;
        case 43:  bg = COLOR_YELLOW;                          doc = do8;   break;
        case 44:  bg = COLOR_BLUE;                            doc = do8;   break;
        case 45:  bg = COLOR_MAGENTA;                         doc = do8;   break;
        case 46:  bg = COLOR_CYAN;                            doc = do8;   break;
        case 47:  bg = COLOR_WHITE;                           doc = do8;   break;
        case 48:  bg = P0(i+1) == 5? P0(i+2) : s->bg; i += 2; doc = do256; break;
        case 49:  bg = -1;                                    doc = true;  break;
        case 90:  fg = COLOR_BLACK;                           doc = do16;  break;
        case 91:  fg = COLOR_RED;                             doc = do16;  break;
        case 92:  fg = COLOR_GREEN;                           doc = do16;  break;
        case 93:  fg = COLOR_YELLOW;                          doc = do16;  break;
        case 94:  fg = COLOR_BLUE;                            doc = do16;  break;
        case 95:  fg = COLOR_MAGENTA;                         doc = do16;  break;
        case 96:  fg = COLOR_CYAN;                            doc = do16;  break;
        case 97:  fg = COLOR_WHITE;                           doc = do16;  break;
        case 100: bg = COLOR_BLACK;                           doc = do16;  break;
        case 101: bg = COLOR_RED;                             doc = do16;  break;
        case 102: bg = COLOR_GREEN;                           doc = do16;  break;
        case 103: bg = COLOR_YELLOW;                          doc = do16;  break;
        case 104: bg = COLOR_BLUE;                            doc = do16;  break;
        case 105: bg = COLOR_MAGENTA;                         doc = do16;  break;
        case 106: bg = COLOR_CYAN;                            doc = do16;  break;
        case 107: bg = COLOR_WHITE;                           doc = do16;  break;
        #if defined(A_ITALIC) && !defined(NO_ITALICS) && !defined(REVERSE_ITALICS)
        case  3:  wattron(win,  A_ITALIC);                    break;
        case 23:  wattroff(win, A_ITALIC);                    break;
        #endif
	#if defined(REVERSE_ITALICS)
	case  3:  wattron(win,  A_REVERSE);                   break;
	case 23:  wattroff(win, A_REVERSE);                   break;
	#endif
    }
    if (doc){
        int p = mtm_alloc_pair(s->fg = fg, s->bg = bg);
        wcolor_set(win, p, NULL);
        cchar_t c;
        setcchar(&c, L" ", A_NORMAL, p, NULL);
        wbkgrndset(win, &c);
   }
}

HANDLER(cr) /* CR - Carriage Return */
    s->xenl = false;
    wmove(win, py, 0);
ENDHANDLER

HANDLER(ind) /* IND - Index */
    y == (bot - 1)? scroll(win) : wmove(win, py + 1, x);
ENDHANDLER

HANDLER(nel) /* NEL - Next Line */
    CALL(cr); CALL(ind);
ENDHANDLER

HANDLER(pnl) /* NL - Newline */
    CALL((n->lnm? nel : ind));
ENDHANDLER

HANDLER(cpl) /* CPL - Cursor Previous Line */
    wmove(win, MAX(tos + top, py - P1(0)), 0);
ENDHANDLER

HANDLER(cnl) /* CNL - Cursor Next Line */
    wmove(win, MIN(tos + bot - 1, py + P1(0)), 0);
ENDHANDLER

HANDLER(print) /* Print a character to the terminal */
    if (wcwidth(w) < 0)
        return;

    if (s->insert)
        CALL(ich);

    if (s->xenl){
        s->xenl = false;
        if (n->am)
            CALL(nel);
        getyx(win, y, x);
        y -= tos;
    }

    if (w < MAXMAP && n->gc[w])
        w = n->gc[w];
    n->repc = w;

    if (x == mx - wcwidth(w)){
        s->xenl = true;
        wins_nwstr(win, &w, 1);
    } else
        waddnwstr(win, &w, 1);
    n->gc = n->gs;
} /* no ENDHANDLER because we don't want to reset repc */

HANDLER(rep) /* REP - Repeat Character */
    for (int i = 0; i < P1(0) && n->repc; i++)
        print(v, p, n->repc, 0, 0, NULL, NULL);
ENDHANDLER

HANDLER(scs) /* Select Character Set */
    wchar_t **t = NULL;
    switch (iw){
        case L'(': t = &n->g0;  break;
        case L')': t = &n->g1;  break;
        case L'*': t = &n->g2;  break;
        case L'+': t = &n->g3;  break;
        default: return;        break;
    }
    switch (w){
        case L'A': *t = CSET_UK;    break;
        case L'B': *t = CSET_US;    break;
        case L'0': *t = CSET_GRAPH; break;
        case L'1': *t = CSET_US;    break;
        case L'2': *t = CSET_GRAPH; break;
    }
ENDHANDLER

HANDLER(so) /* Switch Out/In Character Set */
    if (w == 0x0e)
        n->gs = n->gc = n->g1; /* locking shift */
    else if (w == 0xf)
        n->gs = n->gc = n->g0; /* locking shift */
    else if (w == L'n')
        n->gs = n->gc = n->g2; /* locking shift */
    else if (w == L'o')
        n->gs = n->gc = n->g3; /* locking shift */
    else if (w == L'N'){
        n->gs = n->gc; /* non-locking shift */
        n->gc = n->g2;
    } else if (w == L'O'){
        n->gs = n->gc; /* non-locking shift */
        n->gc = n->g3;
    }
ENDHANDLER

static void
setupevents(NODE *n)
{
    n->vp.p = n;
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x05, ack);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x07, bell);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x08, cub);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x09, tab);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x0a, pnl);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x0b, pnl);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x0c, pnl);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x0d, cr);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x0e, so);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x0f, so);
    vtonevent(&n->vp, VTPARSER_CSI,     L'A', cuu);
    vtonevent(&n->vp, VTPARSER_CSI,     L'B', cud);
    vtonevent(&n->vp, VTPARSER_CSI,     L'C', cuf);
    vtonevent(&n->vp, VTPARSER_CSI,     L'D', cub);
    vtonevent(&n->vp, VTPARSER_CSI,     L'E', cnl);
    vtonevent(&n->vp, VTPARSER_CSI,     L'F', cpl);
    vtonevent(&n->vp, VTPARSER_CSI,     L'G', hpa);
    vtonevent(&n->vp, VTPARSER_CSI,     L'H', cup);
    vtonevent(&n->vp, VTPARSER_CSI,     L'I', tab);
    vtonevent(&n->vp, VTPARSER_CSI,     L'J', ed);
    vtonevent(&n->vp, VTPARSER_CSI,     L'K', el);
    vtonevent(&n->vp, VTPARSER_CSI,     L'L', idl);
    vtonevent(&n->vp, VTPARSER_CSI,     L'M', idl);
    vtonevent(&n->vp, VTPARSER_CSI,     L'P', dch);
    vtonevent(&n->vp, VTPARSER_CSI,     L'S', su);
    vtonevent(&n->vp, VTPARSER_CSI,     L'T', su);
    vtonevent(&n->vp, VTPARSER_CSI,     L'X', ech);
    vtonevent(&n->vp, VTPARSER_CSI,     L'Z', tab);
    vtonevent(&n->vp, VTPARSER_CSI,     L'`', hpa);
    vtonevent(&n->vp, VTPARSER_CSI,     L'^', su);
    vtonevent(&n->vp, VTPARSER_CSI,     L'@', ich);
    vtonevent(&n->vp, VTPARSER_CSI,     L'a', hpr);
    vtonevent(&n->vp, VTPARSER_CSI,     L'b', rep);
    vtonevent(&n->vp, VTPARSER_CSI,     L'c', decid);
    vtonevent(&n->vp, VTPARSER_CSI,     L'd', vpa);
    vtonevent(&n->vp, VTPARSER_CSI,     L'e', vpr);
    vtonevent(&n->vp, VTPARSER_CSI,     L'f', cup);
    vtonevent(&n->vp, VTPARSER_CSI,     L'g', tbc);
    vtonevent(&n->vp, VTPARSER_CSI,     L'h', mode);
    vtonevent(&n->vp, VTPARSER_CSI,     L'l', mode);
    vtonevent(&n->vp, VTPARSER_CSI,     L'm', sgr);
    vtonevent(&n->vp, VTPARSER_CSI,     L'n', dsr);
    vtonevent(&n->vp, VTPARSER_CSI,     L'r', csr);
    vtonevent(&n->vp, VTPARSER_CSI,     L's', sc);
    vtonevent(&n->vp, VTPARSER_CSI,     L'u', rc);
    vtonevent(&n->vp, VTPARSER_CSI,     L'x', decreqtparm);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'0', scs);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'1', scs);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'2', scs);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'7', sc);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'8', rc);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'A', scs);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'B', scs);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'D', ind);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'E', nel);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'H', hts);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'M', ri);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'Z', decid);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'c', ris);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'p', vis);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'=', numkp);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'>', numkp);
    vtonevent(&n->vp, VTPARSER_PRINT,   0,    print);
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
    for (int i = 0; i < w; i++) /* keep old overlapping tabs */
        tabs[i] = i < ow? oldtabs[i] : (i % 8 == 0);
    return tabs;
}

static NODE *
newnode(Node t, NODE *p, int y, int x, int h, int w) /* Create a new node. */
{
    NODE *n = calloc(1, sizeof(NODE));
    bool *tabs = newtabs(w, 0, NULL);
    if (!n || h < 2 || w < 2 || !tabs)
        return free(n), free(tabs), NULL;

    n->t = t;
    n->pt = -1;
    n->p = p;
    n->y = y;
    n->x = x;
    n->h = h;
    n->w = w;
    n->tabs = tabs;
    n->ntabs = w;

    return n;
}

static void
freenode(NODE *n, bool recurse) /* Free a node. */
{
    if (n){
        if (lastfocused == n)
            lastfocused = NULL;
        if (n->pri.win)
            delwin(n->pri.win);
        if (n->alt.win)
            delwin(n->alt.win);
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
        curs_set(focused->s->off != focused->s->tos? 0 : focused->s->vis);
        getyx(focused->s->win, y, x);
        y = MIN(MAX(y, focused->s->tos), focused->s->tos + focused->h - 1);
        wmove(focused->s->win, y, x);
    }
}

static const char *
getterm(void)
{
    const char *envterm = getenv("TERM");
    if (term)
        return term;
    if (envterm && COLORS >= 256 && !strstr(DEFAULT_TERMINAL, "-256color"))
        return DEFAULT_256_COLOR_TERMINAL;
    return DEFAULT_TERMINAL;
}

static NODE *
newview(NODE *p, int y, int x, int h, int w) /* Open a new view. */
{
    struct winsize ws = {.ws_row = h, .ws_col = w};
    NODE *n = newnode(VIEW, p, y, x, h, w);
    if (!n)
        return NULL;

    SCRN *pri = &n->pri, *alt = &n->alt;
    pri->win = newpad(MAX(h, SCROLLBACK), w);
    alt->win = newpad(h, w);
    if (!pri->win || !alt->win)
        return freenode(n, false), NULL;
    pri->tos = pri->off = MAX(0, SCROLLBACK - h);
    n->s = pri;

    nodelay(pri->win, TRUE); nodelay(alt->win, TRUE);
    scrollok(pri->win, TRUE); scrollok(alt->win, TRUE);
    keypad(pri->win, TRUE); keypad(alt->win, TRUE);

    setupevents(n);
    ris(&n->vp, n, L'c', 0, 0, NULL, NULL);

    pid_t pid = forkpty(&n->pt, NULL, NULL, &ws);
    if (pid < 0){
        if (!p)
            perror("forkpty");
        return freenode(n, false), NULL;
    } else if (pid == 0){
        char buf[100] = {0};
        snprintf(buf, sizeof(buf) - 1, "%lu", (unsigned long)getppid());
        setsid();
        setenv("MTM", buf, 1);
        setenv("TERM", getterm(), 1);
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
newcontainer(Node t, NODE *p, int y, int x, int h, int w,
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
        lastfocused = focused;
        focused = n;
    } else
        focus(n->c1? n->c1 : n->c2);
}

#define ABOVE(n) n->y - 2, n->x + n->w / 2
#define BELOW(n) n->y + n->h + 2, n->x + n->w / 2
#define LEFT(n)  n->y + n->h / 2, n->x - 2
#define RIGHT(n) n->y + n->h / 2, n->x + n->w + 2

static NODE *
findnode(NODE *n, int y, int x) /* Find the node enclosing y,x. */
{
    #define IN(n, y, x) (y >= n->y && y <= n->y + n->h && \
                         x >= n->x && x <= n->x + n->w)
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
reshapeview(NODE *n, int d, int ow) /* Reshape a view. */
{
    int oy, ox;
    bool *tabs = newtabs(n->w, ow, n->tabs);
    struct winsize ws = {.ws_row = n->h, .ws_col = n->w};

    if (tabs){
        free(n->tabs);
        n->tabs = tabs;
        n->ntabs = n->w;
    }

    getyx(n->s->win, oy, ox);
    wresize(n->pri.win, MAX(n->h, SCROLLBACK), MAX(n->w, 2));
    wresize(n->alt.win, MAX(n->h, 2), MAX(n->w, 2));
    n->pri.tos = n->pri.off = MAX(0, SCROLLBACK - n->h);
    n->alt.tos = n->alt.off = 0;
    wsetscrreg(n->pri.win, 0, MAX(SCROLLBACK, n->h) - 1);
    wsetscrreg(n->alt.win, 0, n->h - 1);
    if (d > 0){ /* make sure the new top line syncs up after reshape */
        wmove(n->s->win, oy + d, ox);
        wscrl(n->s->win, -d);
    }
    doupdate();
    refresh();
    ioctl(n->pt, TIOCSWINSZ, &ws);
}

static void
reshapechildren(NODE *n) /* Reshape all children of a view. */
{
    if (n->t == HORIZONTAL){
        int i = n->w % 2? 0 : 1;
        reshape(n->c1, n->y, n->x, n->h, n->w / 2);
        reshape(n->c2, n->y, n->x + n->w / 2 + 1, n->h, n->w / 2 - i);
    } else if (n->t == VERTICAL){
        int i = n->h % 2? 0 : 1;
        reshape(n->c1, n->y, n->x, n->h / 2, n->w);
        reshape(n->c2, n->y + n->h / 2 + 1, n->x, n->h / 2 - i, n->w);
    }
}

static void
reshape(NODE *n, int y, int x, int h, int w) /* Reshape a node. */
{
    if (n->y == y && n->x == x && n->h == h && n->w == w && n->t == VIEW)
        return;

    int d = n->h - h;
    int ow = n->w;
    n->y = y;
    n->x = x;
    n->h = MAX(h, 1);
    n->w = MAX(w, 1);

    if (n->t == VIEW)
        reshapeview(n, d, ow);
    else
        reshapechildren(n);
    draw(n);
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
        pnoutrefresh(n->s->win, n->s->off, 0, n->y, n->x,
                     n->y + n->h - 1, n->x + n->w - 1);
    else
        drawchildren(n);
}

static void
split(NODE *n, Node t) /* Split a node. */
{
    int nh = t == VERTICAL? (n->h - 1) / 2 : n->h;
    int nw = t == HORIZONTAL? (n->w) / 2 : n->w;
    NODE *p = n->p;
    NODE *v = newview(NULL, 0, 0, MAX(0, nh), MAX(0, nw));
    if (!v)
        return;

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

    if (n && n->t == VIEW && n->pt > 0 && FD_ISSET(n->pt, f)){
        ssize_t r = read(n->pt, iobuf, sizeof(iobuf));
        if (r > 0)
            vtwrite(&n->vp, iobuf, r);
        if (r <= 0 && errno != EINTR && errno != EWOULDBLOCK)
            return deletenode(n), false;
    }

    return true;
}

static void
scrollback(NODE *n)
{
    n->s->off = MAX(0, n->s->off - n->h / 2);
}

static void
scrollforward(NODE *n)
{
    n->s->off = MIN(n->s->tos, n->s->off + n->h / 2);
}

static void
scrollbottom(NODE *n)
{
    n->s->off = n->s->tos;
}

static void
sendarrow(const NODE *n, const char *k)
{
    char buf[100] = {0};
    snprintf(buf, sizeof(buf) - 1, "\033%s%s", n->pnm? "O" : "[", k);
    SEND(n, buf);
}

static bool
handlechar(int r, int k) /* Handle a single input character. */
{
    const char cmdstr[] = {commandkey, 0};
    static bool cmd = false;
    NODE *n = focused;
    #define KERR(i) (r == ERR && (i) == k)
    #define KEY(i)  (r == OK  && (i) == k)
    #define CODE(i) (r == KEY_CODE_YES && (i) == k)
    #define INSCR (n->s->tos != n->s->off)
    #define SB scrollbottom(n)
    #define DO(s, t, a) \
        if (s == cmd && (t)) { a ; cmd = false; return true; }

    DO(cmd,   KERR(k),             return false)
    DO(cmd,   CODE(KEY_RESIZE),    reshape(root, 0, 0, LINES, COLS); SB)
    DO(false, KEY(commandkey),     return cmd = true)
    DO(false, KEY(0),              SENDN(n, "\000", 1); SB)
    DO(false, KEY(L'\n'),          SEND(n, "\n"); SB)
    DO(false, KEY(L'\r'),          SEND(n, n->lnm? "\r\n" : "\r"); SB)
    DO(false, SCROLLUP && INSCR,   scrollback(n))
    DO(false, SCROLLDOWN && INSCR, scrollforward(n))
    DO(false, RECENTER && INSCR,   scrollbottom(n))
    DO(false, CODE(KEY_ENTER),     SEND(n, n->lnm? "\r\n" : "\r"); SB)
    DO(false, CODE(KEY_UP),        sendarrow(n, "A"); SB);
    DO(false, CODE(KEY_DOWN),      sendarrow(n, "B"); SB);
    DO(false, CODE(KEY_RIGHT),     sendarrow(n, "C"); SB);
    DO(false, CODE(KEY_LEFT),      sendarrow(n, "D"); SB);
    DO(false, CODE(KEY_HOME),      SEND(n, "\033[1~"); SB)
    DO(false, CODE(KEY_END),       SEND(n, "\033[4~"); SB)
    DO(false, CODE(KEY_PPAGE),     SEND(n, "\033[5~"); SB)
    DO(false, CODE(KEY_NPAGE),     SEND(n, "\033[6~"); SB)
    DO(false, CODE(KEY_BACKSPACE), SEND(n, "\177"); SB)
    DO(false, CODE(KEY_DC),        SEND(n, "\033[3~"); SB)
    DO(false, CODE(KEY_IC),        SEND(n, "\033[2~"); SB)
    DO(false, CODE(KEY_BTAB),      SEND(n, "\033[Z"); SB)
    DO(false, CODE(KEY_F(1)),      SEND(n, "\033OP"); SB)
    DO(false, CODE(KEY_F(2)),      SEND(n, "\033OQ"); SB)
    DO(false, CODE(KEY_F(3)),      SEND(n, "\033OR"); SB)
    DO(false, CODE(KEY_F(4)),      SEND(n, "\033OS"); SB)
    DO(false, CODE(KEY_F(5)),      SEND(n, "\033[15~"); SB)
    DO(false, CODE(KEY_F(6)),      SEND(n, "\033[17~"); SB)
    DO(false, CODE(KEY_F(7)),      SEND(n, "\033[18~"); SB)
    DO(false, CODE(KEY_F(8)),      SEND(n, "\033[19~"); SB)
    DO(false, CODE(KEY_F(9)),      SEND(n, "\033[20~"); SB)
    DO(false, CODE(KEY_F(10)),     SEND(n, "\033[21~"); SB)
    DO(false, CODE(KEY_F(11)),     SEND(n, "\033[23~"); SB)
    DO(false, CODE(KEY_F(12)),     SEND(n, "\033[24~"); SB)
    DO(true,  MOVE_UP,             focus(findnode(root, ABOVE(n))))
    DO(true,  MOVE_DOWN,           focus(findnode(root, BELOW(n))))
    DO(true,  MOVE_LEFT,           focus(findnode(root, LEFT(n))))
    DO(true,  MOVE_RIGHT,          focus(findnode(root, RIGHT(n))))
    DO(true,  MOVE_OTHER,          focus(lastfocused))
    DO(true,  HSPLIT,              split(n, HORIZONTAL))
    DO(true,  VSPLIT,              split(n, VERTICAL))
    DO(true,  DELETE_NODE,         deletenode(n))
    DO(true,  BAILOUT,             (void)1)
    DO(true,  NUKE,                wclear(n->s->win); SB)
    DO(true,  REDRAW,              touchwin(stdscr); draw(root); redrawwin(stdscr))
    DO(true,  SCROLLUP,            scrollback(n))
    DO(true,  SCROLLDOWN,          scrollforward(n))
    DO(true,  RECENTER,            scrollbottom(n))
    DO(true,  KEY(commandkey),     SENDN(n, cmdstr, 1));
    char c[MB_LEN_MAX + 1] = {0};
    if (wctomb(c, k) > 0){
        scrollbottom(n);
        SEND(n, c);
    }
    return cmd = false, true;
}

static void
run(void) /* Run MTM. */
{
    while (root){
        wint_t w = 0;
        fd_set sfds = fds;
        if (select(nfds + 1, &sfds, NULL, NULL, NULL) < 0)
            FD_ZERO(&sfds);

        int r = wget_wch(focused->s->win, &w);
        while (handlechar(r, w))
            r = wget_wch(focused->s->win, &w);
        getinput(root, &sfds);

        draw(root);
        doupdate();
        fixcursor();
        draw(focused);
        doupdate();
    }
}

int
main(int argc, char **argv)
{
    FD_SET(STDIN_FILENO, &fds);
    setlocale(LC_ALL, "");
    signal(SIGCHLD, SIG_IGN); /* automatically reap children */

    int c = 0;
    while ((c = getopt(argc, argv, "c:T:t:")) != -1) switch (c){
        case 'c': commandkey = CTL(optarg[0]);      break;
        case 'T': setenv("TERM", optarg, 1);        break;
        case 't': term = optarg;                    break;
        default:  quit(EXIT_FAILURE, USAGE);        break;
    }

    if (!initscr())
        quit(EXIT_FAILURE, "could not initialize terminal");
    ESCDELAY = ESCAPE_TIME;
    raw();
    noecho();
    nonl();
    intrflush(stdscr, FALSE);
    start_color();
    use_default_colors();
    start_pairs();

    root = newview(NULL, 0, 0, LINES, COLS);
    if (!root)
        quit(EXIT_FAILURE, "could not open root window");
    focus(root);
    draw(root);
    run();

    quit(EXIT_SUCCESS, NULL);
    return EXIT_SUCCESS; /* not reached */
}

