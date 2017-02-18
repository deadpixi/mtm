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

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE_EXTENDED

#include <errno.h>
#include <locale.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef NCURSESW_INCLUDE_H
#define NCURSESW_INCLUDE_H <ncursesw/curses.h>
#endif

#include NCURSESW_INCLUDE_H

#include "tmt.h"

#define CTL(x) ((x) & 0x1f)
#define TOPAIR(fg, bg) (monochrome? 0 : (((fg) * 8) | (bg)))
#define USAGE "usage: mtm [-m] [-e MILLISECONDS] [-c KEY]"
#define BUFMAX 1000

#ifdef __GNUC__
#define UNUSED __attribute__((__unused__))
#else
#define UNUSED
#endif

typedef enum{
    HORIZONTAL,
    VERTICAL,
    VIEW
} node_t;

typedef struct NODE NODE;
struct NODE{
    node_t t;

    NODE *p, *c1, *c2;
    int y, x, h, w;

    WINDOW *win;
    TMT *vt;
    int pt;
    pid_t pid;
};

static NODE *root;
static NODE *focused;
static bool monochrome;
static bool needresize = true;
static int commandkey = CTL('g');

static void reshape(NODE *n, int y, int x, int h, int w);
static void draw(NODE *n, bool force);
static void drawview(NODE *n, bool force);
static void reshapechildren(NODE *n);

static const char *
getshell(void)
{
    if (getenv("SHELL")) return getenv("SHELL");

    struct passwd *pwd = getpwuid(getuid());
    if (pwd) return pwd->pw_shell;

    return "/bin/sh";
}

static NODE *
newnode(node_t t, NODE *p, int y, int x, int h, int w)
{
    NODE *n = calloc(1, sizeof(NODE));
    if (!n || h < 2 || w < 2) return free(n), NULL;

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
freenode(NODE *n, bool recurse)
{
    if (n){
        if (n->pt >= 0) close(n->pt); 
        if (n->win) delwin(n->win);
        if (n->vt) tmt_close(n->vt); 
        if (recurse) freenode(n->c1, true);
        if (recurse) freenode(n->c2, true);
        free(n);
    }
}

static void
quit(int rc, const char *m)
{
    if (m) fprintf(stderr, "%s\n", m);
    freenode(root, true);
    endwin();
    exit(rc);
}

static void
fixcursor(void)
{
    if (focused){
        const TMTPOINT *c = tmt_cursor(focused->vt);
        wmove(focused->win, c->r, c->c);
        wrefresh(focused->win);
    }
}

void
callback(tmt_msg_t m, struct TMT *v UNUSED, const void *r UNUSED, void *p)
{
    if (m == TMT_MSG_BELL) beep();
    if (m == TMT_MSG_UPDATE) drawview((NODE *)p, false);
}

static NODE *
newview(NODE *p, int y, int x, int h, int w)
{
    struct winsize ws = {.ws_row = h, .ws_col = w};
    NODE *n = newnode(VIEW, p, y, x, h, w);
    if (!n) return NULL;

    n->win = newwin(h, w, y, x);
    keypad(n->win, TRUE);
    if (!n->win) return freenode(n, false), NULL;

    n->vt = tmt_open(h, w, callback, n);
    if (!n) return freenode(n, false), NULL;

    n->pid = forkpty(&n->pt, NULL, NULL, &ws);
    if (n->pid < 0)
        return freenode(n, false), NULL;
    else if (n->pid == 0){
        setsid();
        setenv("TERM", monochrome? "mach" : "mach-color", 1);
        execl(getshell(), getshell(), NULL);
        return NULL;
    }

    return n;
}

static NODE *
newcontainer(node_t t, NODE *p, int y, int x, int h, int w, NODE *c1, NODE *c2)
{
    NODE *n = newnode(t, p, y, x, h, w);
    if (!n) return NULL;

    n->c1 = c1;
    n->c2 = c2;
    c1->p = c2->p = n;

    reshapechildren(n);
    return n;
}

static void
focus(NODE *n)
{
    if (!n)
        return;
    else if (n->t == VIEW){
        focused = n;
        wrefresh(n->win);
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
findnode(NODE *n, int y, int x)
{
    if (IN(n, y, x)){
        if (n->c1 && IN(n->c1, y, x)) return findnode(n->c1, y, x);
        if (n->c2 && IN(n->c2, y, x)) return findnode(n->c2, y, x);
        return n;
    } else
        return NULL;
}

static void
replacechild(NODE *n, NODE *c1, NODE *c2)
{
    c2->p = n;

    if (!n){
        root = c2;
        reshape(c2, 0, 0, LINES, COLS);
        c2->p = NULL;
    } else if (n->c1 == c1)
        n->c1 = c2;
    else if (n->c2 == c1)
        n->c2 = c2;

    n = n? n : root;
    reshape(n, n->y, n->x, n->h, n->w);
    draw(n, true);
}

static void
removechild(NODE *p, const NODE *c)
{
    replacechild(p->p, p, c == p->c1? p->c2 : p->c1);
    freenode(p, false);
}

static void
deletenode(NODE *n)
{
    if (!n || !n->p) quit(EXIT_SUCCESS, NULL);
    if (n == focused) focus(n->p->c1 == n? n->p->c2 : n->p->c1);

    removechild(n->p, n);
    freenode(n, true);
}

static void
reshapeview(NODE *n, int y, int x, int h, int w)
{
    struct winsize ws = {.ws_row = h, .ws_col = w};
    wresize(n->win, 1, 1);
    mvwin(n->win, y, x);
    wresize(n->win, h? h : 1, w? w : 1);
    if (h >= 2 && w >= 2) tmt_resize(n->vt, h, w);
    ioctl(n->pt, TIOCSWINSZ, &ws);
}

static void
reshapechildren(NODE *n)
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
reshape(NODE *n, int y, int x, int h, int w)
{
    n->y = y;
    n->x = x;
    n->h = h? h : 1;
    n->w = w? w : 1;

    if (n->t == VIEW)
        reshapeview(n, y, x, h, w);
    else
        reshapechildren(n);

    draw(n, true);
}

static cchar_t *
buildcchar(TMTCHAR c)
{
    static cchar_t r = {0};
    attr_t a = A_NORMAL;
    wchar_t s[] = {c.c, 0};

    if (c.a.dim)       a |= A_DIM;
    if (c.a.invisible) a |= A_INVIS;
    if (c.a.bold)      a |= A_BOLD;
    if (c.a.underline) a |= A_UNDERLINE;
    if (c.a.blink)     a |= A_BOLD; /* nobody likes blinking */
    if (c.a.reverse)   a |= A_REVERSE;

    setcchar(&r, s, a, TOPAIR(c.a.fg, c.a.bg), NULL);
    return &r;
}

static void
drawview(NODE *n, bool force)
{
    if (!n->vt) return;

    const TMTSCREEN *s = tmt_screen(n->vt);
    for (size_t r = 0; r < s->nline; r++) if (s->lines[r]->dirty || force){
        for (size_t c = 0; c < s->ncol; c++)
            mvwadd_wch(n->win, r, c, buildcchar(s->lines[r]->chars[c]));
    }
    tmt_clean(n->vt);
    wrefresh(n->win);
    refresh();
}

static void
drawchildren(const NODE *n, bool force)
{
    draw(n->c1, force);
    if (n->t == HORIZONTAL)
        mvvline(n->y, n->x + n->w / 2, ACS_VLINE, n->h);
    else
        mvhline(n->y + n->h / 2, n->x, ACS_HLINE, n->w);
    draw(n->c2, force);
}

static void
draw(NODE *n, bool force)
{
    if (n->t == VIEW)
        drawview(n, force);
    else
        drawchildren(n, force);
}

static void
split(NODE *n, node_t t)
{
    NODE *p = n->p;
    NODE *v = newview(NULL, 0, 0, LINES, COLS);
    if (!v) return;

    NODE *c = newcontainer(t, n->p, n->y, n->x, n->h, n->w, n, v);
    if (!c){
        freenode(c, false);
        return;
    }

    replacechild(p, n, c);
    focus(v);
    draw(p? p : root, true);
}

static void
walk(NODE *n, bool (*c)(NODE *n, void *p), void *p)
{
    if (c(n, p)){
        if (n->c1) walk(n->c1, c, p);
        if (n->c2) walk(n->c2, c, p);
    }
}

static bool
addfds(NODE *n, void *p)
{
    if (n->pt >= 0) FD_SET(n->pt, (fd_set *)p);
    return true;
}

static bool
getinput(NODE *n, void *p)
{
    if (n->pt >= 0 && FD_ISSET(n->pt, (fd_set *)p)){
        char buf[BUFMAX + 1] = {0};
        ssize_t r = read(n->pt, buf, BUFMAX);
        if (r < 0) return deletenode(n), false;
        tmt_writemb(n->vt, buf, r);
    }

    return true;
}

static bool
handlechar(int k)
{
    static bool cmd = false;

    if (k == commandkey) return cmd = !cmd;
    if (!cmd) return false;

    #define C(c) case c : case CTL(c)
    cmd = false;
    switch (k){
        case KEY_UP:     focus(findnode(root, ABOVE(focused))); return true;
        case KEY_DOWN:   focus(findnode(root, BELOW(focused))); return true;
        case KEY_LEFT:   focus(findnode(root, LEFT(focused)));  return true;
        case KEY_RIGHT:  focus(findnode(root, RIGHT(focused))); return true;
        C('h'):          split(focused, HORIZONTAL);            return true;
        C('v'):          split(focused, VERTICAL);              return true;
        C('w'):          deletenode(focused);                   return true;
        C('l'):          draw(root, true);                      return true;
    }

    return false;
}

static const char *
curseskeytokeystroke(int k)
{
    switch (k){
        case KEY_DOWN:      return TMT_KEY_DOWN;
        case KEY_UP:        return TMT_KEY_UP;
        case KEY_LEFT:      return TMT_KEY_LEFT;
        case KEY_RIGHT:     return TMT_KEY_RIGHT;
        case KEY_BACKSPACE: return TMT_KEY_BACKSPACE;
        case KEY_F(1):      return TMT_KEY_F1;
        case KEY_F(2):      return TMT_KEY_F2;
        case KEY_F(3):      return TMT_KEY_F3;
        case KEY_F(4):      return TMT_KEY_F4;
        case KEY_F(5):      return TMT_KEY_F5;
        case KEY_F(6):      return TMT_KEY_F6;
        case KEY_F(7):      return TMT_KEY_F7;
        case KEY_F(8):      return TMT_KEY_F8;
        case KEY_F(9):      return TMT_KEY_F9;
        case KEY_F(10):     return TMT_KEY_F10;
        case KEY_NPAGE:     return TMT_KEY_PAGE_DOWN;
        case KEY_PPAGE:     return TMT_KEY_PAGE_UP;
        case KEY_HOME:      return TMT_KEY_HOME;
        case KEY_END:       return TMT_KEY_END;
        default:            return NULL;
    }
}

static void
handleresize(void)
{
    struct winsize ws = {0};
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    resize_term(ws.ws_row, ws.ws_col);
    reshape(root, 0, 0, LINES, COLS);
    draw(root, true);
    refresh();
    fixcursor();
    needresize = false;
}

static void
run(void)
{
    while (root){
        if (needresize) handleresize();

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        walk(root, addfds, &fds);
        if (select(FD_SETSIZE, &fds, NULL, NULL, NULL) < 0){
            if (errno == EINTR) continue;
            quit(EXIT_FAILURE, strerror(errno));
        }

        if (FD_ISSET(STDIN_FILENO, &fds)){
            int c = wgetch(focused->win);
            const char *k = curseskeytokeystroke(c);
            if (!handlechar(c))
                write(focused->pt, k? k : (char *)&c, k? strlen(k) : 1);
        }

        walk(root, getinput, &fds);
        refresh();
        fixcursor();
    }
}

static short
tocolor(tmt_color_t c)
{
    switch (c){
        case TMT_COLOR_BLACK:   return COLOR_BLACK;
        case TMT_COLOR_RED:     return COLOR_RED;
        case TMT_COLOR_GREEN:   return COLOR_GREEN;
        case TMT_COLOR_YELLOW:  return COLOR_YELLOW;
        case TMT_COLOR_BLUE:    return COLOR_BLUE;
        case TMT_COLOR_MAGENTA: return COLOR_MAGENTA;
        case TMT_COLOR_CYAN:    return COLOR_CYAN;
        case TMT_COLOR_WHITE:   return COLOR_WHITE;
        default:                return -1;
    }
}

static void
initcolors(void)
{
    for (int fg = 1; !monochrome && fg < TMT_COLOR_MAX; fg++){
        for (int bg = 1; bg < TMT_COLOR_MAX; bg++) if (TOPAIR(fg, bg))
            init_pair(TOPAIR(fg, bg), tocolor(fg), tocolor(bg));
    }
}

static void
handlesigwinch(int sig UNUSED)
{
    needresize = true;
}

int
main(int argc, char **argv)
{
    int c = 0;

    setlocale(LC_ALL, "");
    while ((c = getopt(argc, argv, "mc:e:")) != -1){
        switch (c){
            case 'm': monochrome = true;                                break;
            case 'c': commandkey = CTL(optarg[0]);                      break;
            case 'e': ESCDELAY = atoi(optarg)? atoi(optarg) : ESCDELAY; break;
            default:  quit(EXIT_FAILURE, USAGE);                        break;
        }
    }

    struct sigaction sa = {.sa_handler = handlesigwinch, 0};
    sigaction(SIGWINCH, &sa, NULL);

    initscr();
    raw();
    noecho();
    start_color();
    initcolors();


    root = newview(NULL, 0, 0, LINES, COLS);
    focus(root);
    run();

    quit(EXIT_SUCCESS, NULL);
    return EXIT_SUCCESS; /* not reached */
}
