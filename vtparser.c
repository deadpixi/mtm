/* Copyright (c) 2017-2019 Rob King
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
#include <stdbool.h>
#include <string.h>
#include "vtparser.h"

/**** DATA TYPES */
#define MAXACTIONS  128

typedef struct ACTION ACTION;
struct ACTION{
    void (*cb)(VTPARSER *p, wchar_t w);
    STATE *next;
};

struct STATE{
    void (*entry)(VTPARSER *v);
    ACTION actions[MAXACTIONS];
};

/**** GLOBALS */
static STATE ground, escape, escape_intermediate, csi_entry,
             csi_ignore, csi_param, csi_intermediate, osc_string;

/**** ACTION FUNCTIONS */
static void
reset(VTPARSER *v)
{
    v->inter = v->narg = v->nosc = 0;
    memset(v->args, 0, sizeof(v->args));
    memset(v->oscbuf, 0, sizeof(v->oscbuf));
}

static void
ignore(VTPARSER *v, wchar_t w)
{
    (void)v; (void)w; /* avoid warnings */
}

static void
collect(VTPARSER *v, wchar_t w)
{
    v->inter = v->inter? v->inter : (int)w;
}

static void
collectosc(VTPARSER *v, wchar_t w)
{
    if (v->nosc < MAXOSC)
        v->oscbuf[v->nosc++] = w;
}

static void
param(VTPARSER *v, wchar_t w)
{
    v->narg = v->narg? v->narg : 1;

    if (w == L';')
        v->args[v->narg++] = 0;
    else if (v->narg < MAXPARAM && v->args[v->narg - 1] < 9999)
        v->args[v->narg - 1] = v->args[v->narg - 1] * 10 + (w - 0x30);
}

#define DO(k, t, f, n, a)                               \
    static void                                         \
    do ## k (VTPARSER *v, wchar_t w)                    \
    {                                                   \
        if (t)                                          \
            f (v, v->p, w, v->inter, n, a, v->oscbuf);  \
    }

DO(control, w < MAXCALLBACK && v->cons[w], v->cons[w], 0, NULL)
DO(escape,  w < MAXCALLBACK && v->escs[w], v->escs[w], v->inter > 0, &v->inter)
DO(csi,     w < MAXCALLBACK && v->csis[w], v->csis[w], v->narg, v->args)
DO(print,   v->print, v->print, 0, NULL)
DO(osc,     v->osc, v->osc, v->nosc, NULL)

/**** PUBLIC FUNCTIONS */
VTCALLBACK
vtonevent(VTPARSER *vp, VtEvent t, wchar_t w, VTCALLBACK cb)
{
    VTCALLBACK o = NULL;
    if (w < MAXCALLBACK) switch (t){
        case VTPARSER_CONTROL: o = vp->cons[w]; vp->cons[w] = cb; break;
        case VTPARSER_ESCAPE:  o = vp->escs[w]; vp->escs[w] = cb; break;
        case VTPARSER_CSI:     o = vp->csis[w]; vp->csis[w] = cb; break;
        case VTPARSER_PRINT:   o = vp->print;   vp->print   = cb; break;
        case VTPARSER_OSC:     o = vp->osc;     vp->osc     = cb; break;
    }

    return o;
}

static void
handlechar(VTPARSER *vp, wchar_t w)
{
    vp->s = vp->s? vp->s : &ground;
    if (w >= MAXACTIONS){
        doprint(vp, w);
        return;
    }

    const ACTION *a = &vp->s->actions[w];
    if (a->cb)
        a->cb(vp, w);
    if (a->next){
        vp->s = a->next;
        if (a->next->entry)
            a->next->entry(vp);
    }
}

void
vtwrite(VTPARSER *vp, const char *s, size_t n)
{
    wchar_t w = 0;
    while (n){
        size_t r = mbrtowc(&w, s, n, &vp->ms);
        switch (r){
            case -2: /* incomplete character, try again */
                return;

            case -1: /* invalid character, skip it */
                w = VTPARSER_BAD_CHAR;
                r = 1;
                break;

            case 0: /* literal zero, write it but advance */
                r = 1;
                break;
        }

        n -= r;
        s += r;
        handlechar(vp, w);
    }
}

/* Include the generated state tables. */
#include "vttable.c"
