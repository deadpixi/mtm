/* Copyright (c) 2017 Rob King
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the copyright holder nor the
 *     names of contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS,
 * COPYRIGHT HOLDERS, OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tmt.h"

#define BUF_MAX 100
#define PAR_MAX 8
#define TAB 8
#define MAX(x, y) (((size_t)(x) > (size_t)(y)) ? (size_t)(x) : (size_t)(y))
#define MIN(x, y) (((size_t)(x) < (size_t)(y)) ? (size_t)(x) : (size_t)(y))
#define CLINE(vt) (vt)->screen.lines[MIN((vt)->curs.r, (vt)->screen.nline - 1)]

#define P0(x) (vt->pars[x])
#define P1(x) (vt->pars[x]? vt->pars[x] : 1)
#define CB(vt, m, a) ((vt)->cb? (vt)->cb(m, vt, a, (vt)->p) : (void)0)

#define COMMON_VARS             \
    TMTSCREEN *s = &vt->screen; \
    TMTPOINT *c = &vt->curs;    \
    TMTLINE *l = CLINE(vt)

#define HANDLER(name) static void name (TMT *vt) { COMMON_VARS; 

struct TMT{
    TMTPOINT curs;
    TMTATTRS attrs;

    bool dirty;
    TMTSCREEN screen;

    TMTCALLBACK cb;
    void *p;

    mbstate_t ms;
    size_t nmb;
    char mb[BUF_MAX + 1];

    size_t pars[PAR_MAX];   
    size_t npar;
    size_t arg;
    enum {S_NUL, S_ESC, S_ARG} state;
};

static TMTATTRS defattrs = {.fg = TMT_COLOR_DEFAULT, .bg = TMT_COLOR_DEFAULT};

static void
dirtylines(TMT *vt, size_t s, size_t e)
{
    for (size_t i = s; i < e; i++)
        vt->dirty = vt->screen.lines[i]->dirty = true;
}

static void
clearline(TMT *vt, TMTLINE *l, size_t s, size_t e)
{
    vt->dirty = l->dirty = true;
    for (size_t i = s; i < e && i < vt->screen.ncol; i++){
        l->chars[i].a = vt->attrs;
        l->chars[i].c = L' ';
    }
}

static void
clearlines(TMT *vt, size_t r, size_t n)
{
    for (size_t i = r; i < r + n && i < vt->screen.nline; i++)
        clearline(vt, vt->screen.lines[i], 0, vt->screen.ncol);
}

static void
scrup(TMT *vt, size_t r, size_t n)
{
    n = MIN(n, vt->screen.nline - 1 - r);

    if (n){
        TMTLINE *buf[n];

        memcpy(buf, vt->screen.lines + r, n * sizeof(TMTLINE *));
        memmove(vt->screen.lines + r, vt->screen.lines + r + n,
                (vt->screen.nline - n - r) * sizeof(TMTLINE *));
        memcpy(vt->screen.lines + (vt->screen.nline - n),
               buf, n * sizeof(TMTLINE *));

        clearlines(vt, vt->screen.nline - n, n);
        dirtylines(vt, r, vt->screen.nline);
    }
}

static void
scrdn(TMT *vt, size_t r, size_t n)
{
    n = MIN(n, vt->screen.nline - 1 - r);

    if (n){
        TMTLINE *buf[n];

        memcpy(buf, vt->screen.lines + (vt->screen.nline - n),
               n * sizeof(TMTLINE *));
        memmove(vt->screen.lines + r + n, vt->screen.lines + r,
                (vt->screen.nline - n - r) * sizeof(TMTLINE *));
        memcpy(vt->screen.lines + r, buf, n * sizeof(TMTLINE *));
    
        clearlines(vt, r, n);
        dirtylines(vt, r, vt->screen.nline);
    }
}

HANDLER(ed)
    size_t b = 0;
    size_t e = s->nline;

    switch (P0(0)){
        case 0: b = c->r + 1; clearline(vt, l, c->c, vt->screen.ncol); break;
        case 1: e = c->r - 1; clearline(vt, l, 0, c->c);               break;
        case 2:  /* use defaults */                                    break;
        default: /* do nothing   */                                    return;
    }

    clearlines(vt, b, e - b);
}

HANDLER(ich)
    size_t n = P1(0);

    if (n > s->ncol - c->c - 1)
        n = s->ncol - c->c - 1;

    memmove(l->chars + c->c + n, l->chars + c->c,
            MIN(s->ncol - 1 - c->c,
            (s->ncol - c->c - n - 1)) * sizeof(TMTCHAR));
    clearline(vt, l, c->c, n);
}

HANDLER(dch)
    size_t n = P1(0);

    if (n > s->ncol - c->c)
        n = s->ncol - c->c;

    memmove(l->chars + c->c, l->chars + c->c + n,
            (s->ncol - c->c - n) * sizeof(TMTCHAR));

    clearline(vt, l, s->ncol - c->c - n, s->ncol - n);
}

HANDLER(el)
    switch (P0(0)){
        case 0: clearline(vt, l, c->c, vt->screen.ncol); break;
        case 1: clearline(vt, l, 0, c->c);               break;
        case 2: clearline(vt, l, 0, vt->screen.ncol);    break;
    }
}

HANDLER(sgr)
    #define FGBG(c) *(P0(i) < 40? &vt->attrs.fg : &vt->attrs.bg) = c
    for (size_t i = 0; i < vt->npar; i++){
        switch (P0(i)){
            case  0: vt->attrs           = defattrs;   break;
            case  1: vt->attrs.bold      = true;       break;
            case  2: vt->attrs.dim       = true;       break;
            case  4: vt->attrs.underline = true;       break;
            case  5: vt->attrs.blink     = true;       break;
            case  7: vt->attrs.reverse   = true;       break;
            case  8: vt->attrs.invisible = true;       break;
            case 24: vt->attrs.underline = false;      break;
            case 27: vt->attrs.reverse   = false;      break;
            case 30: case 40: FGBG(TMT_COLOR_BLACK);   break;
            case 31: case 41: FGBG(TMT_COLOR_RED);     break;
            case 32: case 42: FGBG(TMT_COLOR_GREEN);   break;
            case 33: case 43: FGBG(TMT_COLOR_YELLOW);  break;
            case 34: case 44: FGBG(TMT_COLOR_BLUE);    break;
            case 35: case 45: FGBG(TMT_COLOR_MAGENTA); break;
            case 36: case 46: FGBG(TMT_COLOR_CYAN);    break;
            case 37: case 47: FGBG(TMT_COLOR_WHITE);   break;
            case 39: case 48: FGBG(TMT_COLOR_DEFAULT); break;
        }
    }
}

HANDLER(resetparser)
    memset(vt->pars, 0, sizeof(vt->pars));
    vt->state = vt->npar = vt->arg = 0;
}

HANDLER(consumearg)
    if (vt->npar < PAR_MAX)
        vt->pars[vt->npar++] = vt->arg;
    vt->arg = 0;
}

HANDLER(fixcursor)
    c->r = MIN(c->r, s->nline - 1);
    c->c = MIN(c->c, s->ncol - 1);
}

static bool
handlechar(TMT *vt, wchar_t w)
{
    COMMON_VARS;

    if (w > UCHAR_MAX || w == 0)
        return resetparser(vt), false;

    #define SC(S, C) (((S) << CHAR_BIT) | (int)(C))
    #define ON(S, C, A) case SC(S, C) : A ; return true
    #define DO(S, C, A) case SC(S, C):do {                              \
        consumearg(vt); A; fixcursor(vt); resetparser(vt); return true; \
    } while (false)

    switch (SC(vt->state, w)){
        DO(S_NUL, 0x07, CB(vt, TMT_MSG_BELL, NULL));
        DO(S_NUL, 0x08, if (c->c) c->c--);
        DO(S_NUL, 0x09, for (int i = TAB - c->c % TAB; i > 0; i--) c->c++);
        DO(S_NUL, 0x0a, c->r < s->nline - 1? (void)c->r++ : scrup(vt, 0, 1));
        DO(S_NUL, 0x0d, c->c = 0);
        ON(S_NUL, 0x1b, vt->state = S_ESC);
        ON(S_ESC, 0x1b, vt->state = S_ESC);
        ON(S_ESC, L'c', tmt_reset(vt));
        ON(S_ESC, L'[', vt->state = S_ARG);
        ON(S_ARG, 0x1b, vt->state = S_ESC);
        ON(S_ARG, L';', consumearg(vt));
        ON(S_ARG, L'?', vt->state = S_ARG);
        ON(S_ARG, L'0', vt->arg = vt->arg * 10 + 0);
        ON(S_ARG, L'1', vt->arg = vt->arg * 10 + 1);
        ON(S_ARG, L'2', vt->arg = vt->arg * 10 + 2);
        ON(S_ARG, L'3', vt->arg = vt->arg * 10 + 3);
        ON(S_ARG, L'4', vt->arg = vt->arg * 10 + 4);
        ON(S_ARG, L'5', vt->arg = vt->arg * 10 + 5);
        ON(S_ARG, L'6', vt->arg = vt->arg * 10 + 6);
        ON(S_ARG, L'7', vt->arg = vt->arg * 10 + 7);
        ON(S_ARG, L'8', vt->arg = vt->arg * 10 + 8);
        ON(S_ARG, L'9', vt->arg = vt->arg * 10 + 9);
        DO(S_ARG, L'A', c->r = MAX(c->r - P1(0), 0));
        DO(S_ARG, L'B', c->r = MIN(c->r + P1(0), s->nline - 1));
        DO(S_ARG, L'C', c->c = MIN(c->c + P1(0), s->ncol - 1));
        DO(S_ARG, L'D', c->c = MIN(c->c - P1(0), c->c));
        DO(S_ARG, L'E', c->c = 0; c->r = MIN(c->r + P1(0), s->nline - 1));
        DO(S_ARG, L'F', c->c = 0; c->r = MAX(c->r - P1(0), 0));
        DO(S_ARG, L'G', c->c = MIN(P1(1), s->ncol - 1));
        DO(S_ARG, L'H', c->r = P1(0) - 1; c->c = P1(1) - 1);
        DO(S_ARG, L'J', ed(vt));
        DO(S_ARG, L'K', el(vt));
        DO(S_ARG, L'L', scrdn(vt, c->r, P1(0)));
        DO(S_ARG, L'M', scrup(vt, c->r, P1(0)));
        DO(S_ARG, L'P', dch(vt));
        DO(S_ARG, L'S', scrup(vt, 0, P1(0)));
        DO(S_ARG, L'T', scrdn(vt, 0, P1(0)));
        DO(S_ARG, L'X', clearline(vt, l, c->c, P1(0)));
        DO(S_ARG, L'm', sgr(vt));
        DO(S_ARG, L'@', ich(vt));
    }

    return resetparser(vt), false;
}

static void
notify(TMT *vt, bool update, bool moved)
{
    if (update) CB(vt, TMT_MSG_UPDATE, &vt->screen);
    if (moved) CB(vt, TMT_MSG_MOVED, &vt->curs);
}

static TMTLINE *
allocline(TMT *vt, TMTLINE *o, size_t n, size_t pc)
{
    TMTLINE *l = realloc(o, sizeof(TMTLINE) + n * sizeof(TMTCHAR));
    if (!l)
        return NULL;

    clearline(vt, l, pc, n);
    return l;
}

static void
freelines(TMT *vt, size_t s, size_t n, bool screen)
{
    for (size_t i = s; vt->screen.lines && i < s + n; i++){
        free(vt->screen.lines[i]);
        vt->screen.lines[i] = NULL;
    }

    if (screen)
        free(vt->screen.lines);
}

TMT *
tmt_open(size_t nline, size_t ncol, TMTCALLBACK cb, void *p)
{
    TMT *vt = calloc(1, sizeof(TMT));
    if (!nline || !ncol || !vt)
        return free(vt), NULL;

    vt->cb = cb;
    vt->p = p;

    if (!tmt_resize(vt, nline, ncol))
        return tmt_close(vt), NULL;

    return vt;
}

void
tmt_close(TMT *vt)
{
    freelines(vt, 0, vt->screen.nline, true);
    free(vt);
}

bool
tmt_resize(TMT *vt, size_t nline, size_t ncol)
{
    if (nline < 2 || ncol < 2)
        return false;

    if (nline < vt->screen.nline)
        freelines(vt, nline, vt->screen.nline - nline, false);

    TMTLINE **l = realloc(vt->screen.lines, nline * sizeof(TMTLINE *));
    if (!l) return false;

    size_t pc = vt->screen.ncol;
    vt->screen.lines = l;
    vt->screen.ncol = ncol;
    for (size_t i = 0; i < nline; i++){
        TMTLINE *nl = NULL;
        if (i >= vt->screen.nline)
            nl = vt->screen.lines[i] = allocline(vt, NULL, ncol, 0);
        else
            nl = allocline(vt, vt->screen.lines[i], ncol, pc);

        if (!nl) return false;
        vt->screen.lines[i] = nl;
    }
    vt->screen.nline = nline;

    fixcursor(vt);
    dirtylines(vt, 0, nline);
    notify(vt, true, true);
    return true;
}

static void
writecharatcursor(TMT *vt, wchar_t w)
{
    COMMON_VARS;

    int nc = 1;
    #ifdef TMT_HAS_WCWIDTH
    int wcwidth(wchar_t c);
    if ((nc = wcwidth(w)) <= 0)
        return;
    #endif

    CLINE(vt)->chars[vt->curs.c].c = w;
    CLINE(vt)->chars[vt->curs.c].a = vt->attrs;
    CLINE(vt)->dirty = vt->dirty = true;

    if (c->c < s->ncol - nc)
        c->c += nc;
    else{
        c->c = 0;
        c->r++;
    }

    if (c->r >= s->nline){
        c->r = s->nline - 1;
        scrup(vt, 0, 1);
    }
}

void
tmt_write(TMT *vt, const wchar_t *w, size_t n)
{
    TMTPOINT oc = vt->curs;
    n = n? n : wcslen(w);

    for (size_t i = 0; i < n; i++) if (!handlechar(vt, w[i]))
        writecharatcursor(vt, w[i]);

    notify(vt, vt->dirty, memcmp(&oc, &vt->curs, sizeof(oc)) != 0);
}

static inline size_t
testmbchar(TMT *vt)
{
    mbstate_t ts = vt->ms;
    return vt->nmb? mbrtowc(NULL, vt->mb, vt->nmb, &ts) : (size_t)-2;
}

static inline void
writembchar(TMT *vt, wchar_t *c)
{
    mbrtowc(c, vt->mb, vt->nmb, &vt->ms);
    vt->nmb = 0;
}

void
tmt_writemb(TMT *vt, const char *s, size_t n)
{
    size_t nw = 0, p = 0, nb = 0;
    wchar_t buf[BUF_MAX + 1] = {0};
    n = n? n : strlen(s);

    while (p < n && vt->nmb < BUF_MAX){
        switch (testmbchar(vt)){
            case (size_t)-1: buf[nw++] = TMT_INVALID_CHAR; vt->nmb = 0; break;
            case (size_t)-2: vt->mb[vt->nmb++] = s[p++];                break;
            default: writembchar(vt, buf + nw++);                       break;
        }

        if (nw >= BUF_MAX){
            tmt_write(vt, buf, nw);
            nw = 0;
            wmemset(buf, 0, BUF_MAX + 1);
        }
    }
    tmt_write(vt, buf, nw);

    nb = testmbchar(vt); /* we might've finished a char on the last byte */
    if (nb && nb != (size_t)-1 && nb != (size_t)-2){
        writembchar(vt, buf);
        tmt_write(vt, buf, 1);
    }
}

const TMTSCREEN *
tmt_screen(const TMT *vt)
{
    return &vt->screen;
}

const TMTPOINT *
tmt_cursor(const TMT *vt)
{
    return &vt->curs;
}

void
tmt_clean(TMT *vt)
{
    vt->dirty = false;
    for (size_t i = 0; i < vt->screen.nline; i++)
        vt->screen.lines[i]->dirty = false;
}

void
tmt_reset(TMT *vt)
{
    vt->curs.r = vt->curs.c = 0;
    resetparser(vt);
    vt->attrs = defattrs;
    memset(&vt->ms, 0, sizeof(vt->ms));
    clearlines(vt, 0, vt->screen.nline);
    notify(vt, true, true);
}

bool
tmt_dirty(const TMT *vt)
{
    return vt->dirty;
}
