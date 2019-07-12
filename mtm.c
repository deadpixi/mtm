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

#include "config.h"
#include "vtparser.h"

#define MAXCTABLE 72
#define MAXTITLE 100
#define MIN(x, y) ((x) < (y)? (x) : (y))
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define CTL(x) ((x) & 0x1f)
#define USAGE "usage: mtm [-s] [-T NAME] [-t NAME] [-c KEY]\n"

/*** DATA TYPES */
typedef enum{
    HORIZONTAL,
    VERTICAL,
    VIEW
} Node;

typedef struct NODE NODE;
struct NODE{
    Node t;
    NODE *p, *c1, *c2;
    int y, x, sy, sx, h, w, pt, vis;
    short fg, bg, sfg, sbg, sp;
    bool insert, oxenl, xenl;
    wchar_t title[MAXTITLE + 1];
    attr_t sattr;
    WINDOW *win;
    VTPARSER vp;
};

typedef struct COLORTABLE COLORTABLE;
struct COLORTABLE{
    bool used;
    short f;
    short b;
};

/*** GLOBALS AND PROTOTYPES */
static COLORTABLE ctable[MAXCTABLE];
static NODE *root, *focused, *lastfocused = NULL;
static int commandkey = CTL(COMMAND_KEY), nfds = 1; /* stdin */
static fd_set fds;
static char iobuf[BUFSIZ + 1];
static bool dostatus = false;
static wchar_t title[MAXTITLE + 1];

static void setupevents(NODE *n);
static void reshape(NODE *n, int y, int x, int h, int w);
static void draw(NODE *n);
static void reshapechildren(NODE *n);
static const char *term = "eterm-color";
static void freenode(NODE *n, bool recursive);
static void updatetitle(void);

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
 *      CALL(h)        - Call handler h with no arguments.
 *      SENDN(n, s, c) - Write string c bytes of s to n.
 *      SEND(n, s)     - Write string s to node n's host.
 *      (END)HANDLER   - Declare/end a handler function
 *      COMMONVARS     - All of the common variables for a handler.
 *                       x, y     - cursor position
 *                       mx, my   - max possible values for x and y
 *                       n        - the current node
 *                       win      - the current window
 *                       top, bot - the scrolling region
 * The funny names for handlers are from their ANSI/ECMA/DEC mnemonics.
 */
#define PD(x, d) (argc < (x) || !argv? (d) : argv[(x)])
#define P0(x) PD(x, 0)
#define P1(x) (!P0(x)? 1 : P0(x))
#define CALL(x) (x)(v, n, 0, 0, 0, NULL, NULL);
#define SENDN(n, s, c) safewrite(n->pt, s, c)
#define SEND(n, s) SENDN(n, s, strlen(s))
#define COMMONVARS                                                  \
    NODE *n = (NODE *)p;                                            \
    WINDOW *win = n->win;                                           \
    int y, x, my, mx, top = 0, bot = 0;                             \
    (void)v; (void)p; (void)w; (void)iw; (void)argc; (void)argv;    \
    (void)win; (void)y; (void)x; (void)my; (void)mx; (void)osc;     \
    getyx(win, y, x);                                               \
    getmaxyx(win, my, mx);                                          \
    wgetscrreg(win, &top, &bot);                                    \
    bot++;

#define HANDLER(name)                                   \
    static void                                         \
    name (VTPARSER *v, void *p, wchar_t w, wchar_t iw,  \
          int argc, int *argv, const wchar_t *osc)      \
    { COMMONVARS
#define ENDHANDLER }

HANDLER(bell) /* Terminal bell. */
    beep();
ENDHANDLER

HANDLER(cup) /* CUP - Cursor Position */
    n->xenl = false;
    wmove(win, P1(0) - 1, P1(1) - 1);
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
    wmove(win, MAX(y - P1(0), top), x);
ENDHANDLER

HANDLER(cud) /* CUD - Cursor Down */
    wmove(win, MIN(y + P1(0), bot - 1), x);
ENDHANDLER

HANDLER(cuf) /* CUF - Cursor Forward */
    wmove(win, y, MIN(x + P1(0), mx - 1));
ENDHANDLER

HANDLER(ack) /* ACK - Acknowledge Enquiry */
    SEND(n, "\006");
ENDHANDLER

HANDLER(ri) /* RI - Reverse Index */
    y == top? wscrl(win, -1) : wmove(win, y - 1, x);
ENDHANDLER

HANDLER(hpa) /* HPA - Cursor Horizontal Absolute */
    wmove(win, y, MIN(P1(0) - 1, mx - 1));
ENDHANDLER

HANDLER(sc) /* SC - Save Cursor */
    n->sx = x;                               /* save X position            */
    n->sy = y;                               /* save Y position            */
    wattr_get(win, &n->sattr, &n->sp, NULL); /* save attrs and color pair  */
    n->sfg = n->fg;                          /* save foreground color      */
    n->sbg = n->bg;                          /* save background color      */
    n->oxenl = n->xenl;                      /* save xenl state            */
ENDHANDLER

HANDLER(rc) /* RC - Restore Cursor */
    wmove(win, n->sy, n->sx);                /* get old position          */
    wattr_set(win, n->sattr, n->sp, NULL);   /* get attrs and color pair  */
    n->fg = n->sfg;                          /* get foreground color      */
    n->bg = n->sbg;                          /* get background color      */
    n->xenl = n->oxenl;                      /* get xenl state            */
ENDHANDLER

HANDLER(cub) /* CUB - Cursor Backward */
    n->xenl = false;
    wmove(win, y, MAX(x - P1(0), 0));
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
            el(v, p, w, iw, 1, &o, NULL);
            break;
    }
    wmove(win, y, x);
ENDHANDLER

HANDLER(dsr) /* DSR - Device Status Report */
    if (P0(0) == 6){
        char buf[100] = {0};
        snprintf(buf, 99, "\033[%d;%dR", y + 1, x + 1);
        SEND(n, buf);
    }
ENDHANDLER

HANDLER(idl) /* IL or DL - Insert/Delete Line */
    /* insdelln inserts lines above, not below, so we scroll instead */
    wsetscrreg(win, y, bot - 1);
    wscrl(win, w == L'L'? -P1(0) : P1(0));
    wsetscrreg(win, top, bot - 1);
ENDHANDLER

HANDLER(csr) /* CSR - Change Scrolling Region */
    if (wsetscrreg(win, P1(0) - 1, PD(1, my) - 1) == OK)
        CALL(cup);
ENDHANDLER

HANDLER(mode) /* Set or Reset Mode */
    bool set = (w == L'h');
    for (int i = 0; i < argc; i++) switch (P0(i)){
        case  4: n->insert = set; break;
        case 25: n->vis = set;    break;
    }
ENDHANDLER

HANDLER(sgr0) /* Reset SGR to default */
    wattrset(n->win, A_NORMAL);
    wcolor_set(n->win, 0, NULL);
    n->fg = n->bg = -1;
ENDHANDLER

HANDLER(ris) /* RIS - Reset to Initial State */
    swprintf(n->title, MAXTITLE, L"%s", getshell());
    CALL(sgr0);
    wclear(win);
    wmove(win, 0, 0);
    n->vis = 1;
    n->insert = n->oxenl = n->xenl = false;
    wsetscrreg(win, 0, n->h - 1);
ENDHANDLER

HANDLER(sgr) /* SGR - Select Graphic Rendition */
    bool doc = false;

    if (!argc)
        CALL(sgr0);

    for (int i = 0; i < argc; i++) switch (P0(i)){
        case  0: CALL(sgr0);                        break;
        case  1: wattron(win,  A_BOLD);             break;
        case  4: wattron(win,  A_UNDERLINE);        break;
        case  5: wattron(win,  A_BLINK);            break;
        case  7: wattron(win,  A_REVERSE);          break;
        case  8: wattron(win,  A_INVIS);            break;
        case 24: wattroff(win, A_UNDERLINE);        break;
        case 27: wattroff(win, A_REVERSE);          break;
        case 30: n->fg = COLOR_BLACK;   doc = true; break;
        case 31: n->fg = COLOR_RED;     doc = true; break;
        case 32: n->fg = COLOR_GREEN;   doc = true; break;
        case 33: n->fg = COLOR_YELLOW;  doc = true; break;
        case 34: n->fg = COLOR_BLUE;    doc = true; break;
        case 35: n->fg = COLOR_MAGENTA; doc = true; break;
        case 36: n->fg = COLOR_CYAN;    doc = true; break;
        case 37: n->fg = COLOR_WHITE;   doc = true; break;
        case 39: n->fg = -1;            doc = true; break;
        case 40: n->bg = COLOR_BLACK;   doc = true; break;
        case 41: n->bg = COLOR_RED;     doc = true; break;
        case 42: n->bg = COLOR_GREEN;   doc = true; break;
        case 43: n->bg = COLOR_YELLOW;  doc = true; break;
        case 44: n->bg = COLOR_BLUE;    doc = true; break;
        case 45: n->bg = COLOR_MAGENTA; doc = true; break;
        case 46: n->bg = COLOR_CYAN;    doc = true; break;
        case 47: n->bg = COLOR_WHITE;   doc = true; break;
        case 49: n->bg = -1;            doc = true; break;
        #if defined(A_ITALIC) && !defined(NO_ITALICS)
        case  3: wattron(win,  A_ITALIC);           break;
        case 23: wattroff(win, A_ITALIC);           break;
        #endif
    }

    if (doc)
        wcolor_set(win, getpair(n->fg, n->bg), NULL);
}

HANDLER(cr) /* CR - Carriage Return */
    n->xenl = false;
    wmove(win, y, 0);
ENDHANDLER

HANDLER(ind) /* IND - Index */
    y == bot - 1? scroll(win) : wmove(win, y + 1, x);
ENDHANDLER

HANDLER(osc) /* OSC - Operating System Command */
    if (wcslen(osc) >= 2 && osc[1] == L';'){
        wcsncpy(n->title, osc + 2, MAXTITLE - 1);
        if (n == focused)
            updatetitle();
    }
ENDHANDLER

HANDLER(print) /* Print a character to the terminal */
    if (wcwidth(w) < 0)
        return;

    if (n->insert)
        CALL(ich);

    if (n->xenl){
        n->xenl = false;
        CALL(cr);
        CALL(ind);
        getyx(win, y, x);
    }

    if (x == mx - wcwidth(w)){
        n->xenl = true;
        wins_nwstr(win, &w, 1);
    } else
        waddnwstr(win, &w, 1);

    wnoutrefresh(win);
ENDHANDLER

HANDLER(tab) /* HT - Horizontal tab */
    while (++x % 8 && x < mx) ;
    wmove(win, y, x);
ENDHANDLER

static void
setupevents(NODE *n)
{
    n->vp.p = n;
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x05, ack);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x07, bell);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x08, cub);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x09, tab);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x0a, ind);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x0b, ind);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x0c, ind);
    vtonevent(&n->vp, VTPARSER_CONTROL, 0x0d, cr);
    vtonevent(&n->vp, VTPARSER_CSI,     L'A', cuu);
    vtonevent(&n->vp, VTPARSER_CSI,     L'B', cud);
    vtonevent(&n->vp, VTPARSER_CSI,     L'C', cuf);
    vtonevent(&n->vp, VTPARSER_CSI,     L'D', cub);
    vtonevent(&n->vp, VTPARSER_CSI,     L'G', hpa);
    vtonevent(&n->vp, VTPARSER_CSI,     L'H', cup);
    vtonevent(&n->vp, VTPARSER_CSI,     L'J', ed);
    vtonevent(&n->vp, VTPARSER_CSI,     L'K', el);
    vtonevent(&n->vp, VTPARSER_CSI,     L'L', idl);
    vtonevent(&n->vp, VTPARSER_CSI,     L'M', idl);
    vtonevent(&n->vp, VTPARSER_CSI,     L'P', dch);
    vtonevent(&n->vp, VTPARSER_CSI,     L'@', ich);
    vtonevent(&n->vp, VTPARSER_CSI,     L'f', cup);
    vtonevent(&n->vp, VTPARSER_CSI,     L'h', mode);
    vtonevent(&n->vp, VTPARSER_CSI,     L'l', mode);
    vtonevent(&n->vp, VTPARSER_CSI,     L'm', sgr);
    vtonevent(&n->vp, VTPARSER_CSI,     L'n', dsr);
    vtonevent(&n->vp, VTPARSER_CSI,     L'r', csr);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'7', sc);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'8', rc);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'D', ind);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'M', ri);
    vtonevent(&n->vp, VTPARSER_ESCAPE,  L'c', ris);
    vtonevent(&n->vp, VTPARSER_PRINT,   0,    print);
    vtonevent(&n->vp, VTPARSER_OSC,     0,    osc);
}

/*** MTM FUNCTIONS
 * These functions do the user-visible work of MTM: creating nodes in the
 * tree, updating the display, and so on.
 */
static NODE *
newnode(Node t, NODE *p, int y, int x, int h, int w) /* Create a new node. */
{
    NODE *n = calloc(1, sizeof(NODE));
    if (!n || h < 2 || w < 2)
        return free(n), NULL;

    n->t = t;
    n->pt = -1;
    n->p = p;
    n->y = y;
    n->x = x;
    n->h = h;
    n->w = w;

    return n;
}

static void
freenode(NODE *n, bool recurse) /* Free a node. */
{
    if (n){
        if (lastfocused == n)
            lastfocused = NULL;
        if (n->win)
            delwin(n->win);
        if (recurse)
            freenode(n->c1, true);
        if (recurse)
            freenode(n->c2, true);
        if (n->pt >= 0){
            close(n->pt);
            FD_CLR(n->pt, &fds);
        }
        free(n);
    }
}

static void
updatetitle(void) /* update the current title - XXX this is remarkbly inefficient */
{
    if (dostatus && wcsncmp(title, focused->title, MAXTITLE) != 0){
        char newtitle[MAXTITLE + 1] = {0};
        snprintf(newtitle, MAXTITLE, "\033]0;%ls\007", focused->title);
        doupdate();
        putp(newtitle);
        fflush(stdout);
        refresh();
        wcsncpy(title, focused->title, MAXTITLE);
    }
}

static void
fixcursor(void) /* Move the terminal cursor to the active view. */
{
    if (focused){
        int y, x;
        curs_set(focused->vis);
        getyx(focused->win, y, x);
        wmove(focused->win, y, x);
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

    n->win = newwin(h, w, y, x);
    if (!n->win)
        return freenode(n, false), NULL;
    nodelay(n->win, TRUE);
    scrollok(n->win, TRUE);
    keypad(n->win, TRUE);

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
        updatetitle();
        wnoutrefresh(n->win);
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
reshapeview(NODE *n, int y, int x, int h, int w) /* Reshape a view. */
{
    int oy, ox;
    struct winsize ws = {.ws_row = h, .ws_col = w};

    getyx(n->win, oy, ox);
    mvwin(n->win, 0, 0);
    wresize(n->win, h? h : 2, w? w : 2);
    mvwin(n->win, y, x);
    csr(&n->vp, n, L'r', 0, 0, NULL, NULL);
    wmove(n->win, oy, ox);
    wnoutrefresh(n->win);
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
split(NODE *n, Node t) /* Split a node. */
{
    int nh = t == HORIZONTAL?   (n->h - 1) / 2 : n->h;
    int nw = t == VERTICAL? (n->w) / 2 : n->w;
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
        ssize_t r = read(n->pt, iobuf, BUFSIZ);
        if (r > 0)
            vtwrite(&n->vp, iobuf, r);
        if (r < 0 && errno != EINTR && errno != EWOULDBLOCK)
            return deletenode(n), false;
    }

    return true;
}

static bool
handlechar(int r, int k) /* Handle a single input character. */
{
    static bool cmd = false;

    #define KERR(i) (r == ERR && (i) == k)
    #define KEY(i)  (r == OK  && (i) == k)
    #define CODE(i) (r == KEY_CODE_YES && (i) == k)
    #define DO(s, t, a) \
        if (s == cmd && (t)) { a ; cmd = false; return true; }

    DO(cmd,   KERR(k),             return false)
    DO(cmd,   CODE(KEY_RESIZE),    reshape(root, 0, 0, LINES, COLS))
    DO(cmd,   CODE(KEY_BACKSPACE), SEND(focused, "\177"))
    DO(cmd,   CODE(KEY_DC),        SEND(focused, "\033[3~"))
    DO(cmd,   CODE(KEY_IC),        SEND(focused, "\033[2~"))
    DO(false, KEY(commandkey),     return cmd = true)
    DO(false, KEY(0),              SENDN(focused, "\000", 1))
    DO(false, KEY(L'\n'),          SEND(focused, "\n"))
    DO(false, KEY(L'\r'),          SEND(focused, "\r"))
    DO(false, CODE(KEY_ENTER),     SEND(focused, "\r"))
    DO(false, CODE(KEY_UP),        SEND(focused, "\033OA"))
    DO(false, CODE(KEY_DOWN),      SEND(focused, "\033OB"))
    DO(false, CODE(KEY_RIGHT),     SEND(focused, "\033OC"))
    DO(false, CODE(KEY_LEFT),      SEND(focused, "\033OD"))
    DO(false, CODE(KEY_HOME),      SEND(focused, "\033[1~"))
    DO(false, CODE(KEY_END),       SEND(focused, "\033[4~"))
    DO(false, CODE(KEY_PPAGE),     SEND(focused, "\033[5~"))
    DO(false, CODE(KEY_NPAGE),     SEND(focused, "\033[6~"))
    DO(true,  MOVE_UP,             focus(findnode(root, ABOVE(focused))))
    DO(true,  MOVE_DOWN,           focus(findnode(root, BELOW(focused))))
    DO(true,  MOVE_LEFT,           focus(findnode(root, LEFT(focused))))
    DO(true,  MOVE_RIGHT,          focus(findnode(root, RIGHT(focused))))
    DO(true,  MOVE_OTHER,          focus(lastfocused))
    DO(true,  HSPLIT,              split(focused, HORIZONTAL))
    DO(true,  VSPLIT,              split(focused, VERTICAL))
    DO(true,  DELETE_NODE,         deletenode(focused))
    DO(true,  REDRAW,              touchwin(stdscr); draw(root); redrawwin(stdscr))

    char c[MB_LEN_MAX + 1] = {0};
    if (wctomb(c, k) > 0)
        SEND(focused, c);

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

        int r = wget_wch(focused->win, &w);
        while (handlechar(r, w))
            r = wget_wch(focused->win, &w);
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
    signal(SIGCHLD, SIG_IGN); /* automatically reap children */

    int c = 0;
    while ((c = getopt(argc, argv, "c:T:t:x")) != -1) switch (c){
        case 'c': commandkey = CTL(optarg[0]);      break;
        case 'T': setenv("TERM", optarg, 1);        break;
        case 't': term = optarg;                    break;
        case 'x': dostatus = true;                  break;
        default:  quit(EXIT_FAILURE, USAGE);        break;
    }

    if (!initscr())
        quit(EXIT_FAILURE, "could not initialize terminal");

    raw();
    noecho();
    nonl();
    intrflush(stdscr, FALSE);
    start_color();
    use_default_colors();
    dostatus = (bool)tigetflag("XT") || dostatus;

    root = newview(NULL, 0, 0, LINES, COLS);
    if (!root)
        quit(EXIT_FAILURE, "could not open root window");
    focus(root);
    draw(root);
    run();

    quit(EXIT_SUCCESS, NULL);
    return EXIT_SUCCESS; /* not reached */
}
