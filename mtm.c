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
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include NCURSESW_INCLUDE_H

#include "tmt.h"
#include "config.h"

#define MAX(x, y) ((x) > (y)? (x) : (y))
#define CTL(x) ((x) & 0x1f)
#define USAGE "usage: mtm [-m] [-c KEY]"

typedef enum{
    HORIZONTAL,
    VERTICAL,
    VIEW
} node_t;

typedef struct NODE NODE;
struct NODE{
    node_t t;
    NODE *p, *c1, *c2;
    int y, x, h, w, pt;
    WINDOW *win;
    TMT *vt;
};

static NODE *root, *focused;
static bool monochrome, cmd;
static int commandkey = CTL(COMMAND_KEY), nfds = 1; /* stdin */
static fd_set fds;
static char iobuf[BUFSIZ + 1];
static void reshape(NODE *n, int y, int x, int h, int w);
static void draw(NODE *n, bool force);
static void drawview(NODE *n, bool force);
static void reshapechildren(NODE *n);

#define TOPAIR(fg, bg) (monochrome? 0 :                          \
    COLOR_PAIRS < 80? (((fg == -1? TMT_COLOR_WHITE : fg) << 3) | \
                       (bg == -1? TMT_COLOR_BLACK : bg))       : \
                      ((fg == -1? 128 : fg << 3)               | \
                       (bg == -1? 64  : bg)))

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
        if (n->win) delwin(n->win);
        if (n->vt) tmt_close(n->vt); 
        if (recurse) freenode(n->c1, true);
        if (recurse) freenode(n->c2, true);
        if (n->pt >= 0){
            close(n->pt);
            FD_CLR(n->pt, &fds);
        }
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
safewrite(int fd, const char *b, size_t n)
{
    size_t w = 0;
    while (w < n){
        ssize_t s = write(fd, b + w, n - w);
        if (s < 0 && errno != EINTR) return;
        w += (size_t)s;
    }
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
callback(tmt_msg_t m, struct TMT *v, const void *r, void *p)
{
    switch (m){
        case TMT_MSG_UPDATE: drawview((NODE *)p, false); break;
        case TMT_MSG_MOVED:  /* ignored */               break;
        case TMT_MSG_BELL:   beep();                     break;
    }
}

static NODE *
newview(NODE *p, int y, int x, int h, int w)
{
    struct winsize ws = {.ws_row = h, .ws_col = w};
    NODE *n = newnode(VIEW, p, y, x, h, w);
    if (!n) return NULL;

    n->win = newwin(h, w, y, x);
    keypad(n->win, TRUE);
    nodelay(n->win, TRUE);
    if (!n->win) return freenode(n, false), NULL;

    n->vt = tmt_open(h, w, callback, n);
    if (!n) return freenode(n, false), NULL;

    pid_t pid = forkpty(&n->pt, NULL, NULL, &ws);
    if (pid < 0)
        return freenode(n, false), NULL;
    else if (pid == 0){
        setsid();
        setenv("NCURSES_NO_UTF8_ACS", "1", 1);
        setenv("TERM", monochrome? "mach" : "mach-color", 1);
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
findnode(NODE *n, int y, int x)
{
    if (IN(n, y, x)){
        if (n->c1 && IN(n->c1, y, x)) return findnode(n->c1, y, x);
        if (n->c2 && IN(n->c2, y, x)) return findnode(n->c2, y, x);
        return n;
    }
    return NULL;
}

static void
replacechild(NODE *n, NODE *c1, NODE *c2)
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
    tmt_resize(n->vt, h, w);
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
    static cchar_t r;
    attr_t a = A_NORMAL;
    wchar_t s[] = {c.c, 0};

    if (c.a.dim)               a |= A_DIM;
    if (c.a.invisible)         a |= A_INVIS;
    if (c.a.bold || c.a.blink) a |= A_BOLD;
    if (c.a.underline)         a |= A_UNDERLINE;
    if (c.a.reverse)           a |= A_REVERSE;

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
    wnoutrefresh(n->win);
    wnoutrefresh(stdscr);
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
    int nh = t == VERTICAL?   (n->h - 1) / 2 : n->h;
    int nw = t == HORIZONTAL? (n->w - 1) / 2 : n->w;
    NODE *p = n->p;
    NODE *v = newview(NULL, 0, 0, MAX(0, nh), MAX(0, nw));
    if (!v) return;

    NODE *c = newcontainer(t, n->p, n->y, n->x, n->h, n->w, n, v);
    if (!c){
        freenode(v, false);
        return;
    }

    replacechild(p, n, c);
    focus(v);
    draw(p? p : root, true);
}

static bool
getinput(NODE *n, fd_set *f)
{
    if (n && n->c1 && !getinput(n->c1, f)) return false;
    if (n && n->c2 && !getinput(n->c2, f)) return false;
    if (n && n->pt >= 0 && FD_ISSET(n->pt, f)){
        ssize_t r = read(n->pt, iobuf, BUFSIZ);
        if (r > 0)
            tmt_writemb(n->vt, iobuf, r);
        else if (r < 0){
            if (errno != EINTR && errno != EWOULDBLOCK)
                return deletenode(n), false;
        }
    }
    return true;
}

static bool
handlechar(int k)
{
    #define WRITESTR(s) safewrite(focused->pt, s, strlen(s))
    #define DO(s, i, a) if (s == cmd && i == k) { a ; cmd = false; return true;}

    DO(cmd,   KEY_RESIZE,    reshape(root, 0, 0, LINES, COLS))
    DO(cmd,   ERR,           return false)
    DO(cmd,   commandkey,    return cmd = !cmd)
    DO(false, '\n',          WRITESTR("\r"))
    DO(false, KEY_DOWN,      WRITESTR(TMT_KEY_DOWN))
    DO(false, KEY_UP,        WRITESTR(TMT_KEY_UP))
    DO(false, KEY_LEFT,      WRITESTR(TMT_KEY_LEFT))
    DO(false, KEY_RIGHT,     WRITESTR(TMT_KEY_RIGHT))
    DO(false, KEY_BACKSPACE, WRITESTR(TMT_KEY_BACKSPACE))
    DO(false, KEY_F(1),      WRITESTR(TMT_KEY_F1))
    DO(false, KEY_F(2),      WRITESTR(TMT_KEY_F2))
    DO(false, KEY_F(3),      WRITESTR(TMT_KEY_F3))
    DO(false, KEY_F(4),      WRITESTR(TMT_KEY_F4))
    DO(false, KEY_F(5),      WRITESTR(TMT_KEY_F5))
    DO(false, KEY_F(6),      WRITESTR(TMT_KEY_F6))
    DO(false, KEY_F(7),      WRITESTR(TMT_KEY_F7))
    DO(false, KEY_F(8),      WRITESTR(TMT_KEY_F8))
    DO(false, KEY_F(9),      WRITESTR(TMT_KEY_F9))
    DO(false, KEY_F(10),     WRITESTR(TMT_KEY_F10))
    DO(false, KEY_NPAGE,     WRITESTR(TMT_KEY_PAGE_DOWN))
    DO(false, KEY_PPAGE,     WRITESTR(TMT_KEY_PAGE_UP))
    DO(false, KEY_HOME,      WRITESTR(TMT_KEY_HOME))
    DO(false, KEY_END,       WRITESTR(TMT_KEY_END))
    DO(true,  MOVE_UP,       focus(findnode(root, ABOVE(focused))))
    DO(true,  MOVE_DOWN,     focus(findnode(root, BELOW(focused))))
    DO(true,  MOVE_LEFT,     focus(findnode(root, LEFT(focused))))
    DO(true,  MOVE_RIGHT,    focus(findnode(root, RIGHT(focused))))
    DO(true,  HSPLIT,        split(focused, HORIZONTAL))
    DO(true,  VSPLIT,        split(focused, VERTICAL))
    DO(true,  DELETE_NODE,   deletenode(focused))
    DO(true,  REDRAW,        erase(); draw(root, true))

    char c[] = {(char)k, 0};
    WRITESTR(c);
    return true;
}

static void
run(void)
{
    while (root){
        fd_set sfds = fds;
        if (select(nfds + 1, &sfds, NULL, NULL, NULL) < 0) FD_ZERO(&sfds);
        while (handlechar(wgetch(focused->win))) ;
        getinput(root, &sfds);
        doupdate();
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
    if (COLOR_PAIRS >= 80 || monochrome) use_default_colors();
    for (short fg = -1; !monochrome && fg < TMT_COLOR_MAX; fg++){
        for (short bg = -1; bg < TMT_COLOR_MAX; bg++)
            init_pair(TOPAIR(fg, bg), tocolor(fg), tocolor(bg));
    }
}

int
main(int argc, char **argv)
{
    FD_SET(STDIN_FILENO, &fds);
    setlocale(LC_ALL, "");
    signal(SIGCHLD, SIG_IGN);

    int c = 0;
    while ((c = getopt(argc, argv, "mc:")) != -1){
        switch (c){
            case 'm': monochrome = true;           break;
            case 'c': commandkey = CTL(optarg[0]); break;
            default:  quit(EXIT_FAILURE, USAGE);   break;
        }
    }

    initscr();
    raw();
    noecho();
    start_color();
    monochrome = COLOR_PAIRS < 64? true : monochrome;
    initcolors();
    focus(root = newview(NULL, 0, 0, LINES, COLS));
    draw(root, true);
    run();

    quit(EXIT_SUCCESS, NULL);
    return EXIT_SUCCESS; /* not reached */
}
