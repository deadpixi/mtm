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
    wchar_t lo, hi;
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
    for (ACTION *a = vp->s->actions; a->cb; a++) if (w >= a->lo && w <= a->hi){
        a->cb(vp, w);
        if (a->next){
            vp->s = a->next;
            if (a->next->entry)
                a->next->entry(vp);
        }
        return;
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

/**** STATE DEFINITIONS
 * This was built by consulting the excellent state chart created by
 * Paul Flo Williams: http://vt100.net/emu/dec_ansi_parser
 * Please note that Williams does not (AFAIK) endorse this work.
 */
#define MAKESTATE(name, onentry, ...)         \
    static STATE name ={                      \
        onentry ,                             \
        {                                     \
            {0x00, 0x00, ignore,    NULL},    \
            {0x7f, 0x7f, ignore,    NULL},    \
            {0x18, 0x18, docontrol, &ground}, \
            {0x1a, 0x1a, docontrol, &ground}, \
            {0x1b, 0x1b, ignore,    &escape}, \
            {0x01, 0x06, docontrol, NULL},    \
            {0x08, 0x17, docontrol, NULL},    \
            {0x19, 0x19, docontrol, NULL},    \
            {0x1c, 0x1f, docontrol, NULL},    \
            __VA_ARGS__ ,                     \
            {0x07, 0x07, docontrol, NULL},    \
            {0x00, 0x00, NULL,      NULL}     \
        }                                     \
    }

MAKESTATE(ground, NULL,
    {0x20, WCHAR_MAX, doprint, NULL}
);

MAKESTATE(escape, reset,
    {0x21, 0x21, ignore,   &osc_string},
    {0x20, 0x2f, collect,  &escape_intermediate},
    {0x30, 0x4f, doescape, &ground},
    {0x51, 0x57, doescape, &ground},
    {0x59, 0x59, doescape, &ground},
    {0x5a, 0x5a, doescape, &ground},
    {0x5c, 0x5c, doescape, &ground},
    {0x6b, 0x6b, ignore,   &osc_string},
    {0x60, 0x7e, doescape, &ground},
    {0x5b, 0x5b, ignore,   &csi_entry},
    {0x5d, 0x5d, ignore,   &osc_string},
    {0x5e, 0x5e, ignore,   &osc_string},
    {0x50, 0x50, ignore,   &osc_string},
    {0x5f, 0x5f, ignore,   &osc_string}
);

MAKESTATE(escape_intermediate, NULL,
    {0x20, 0x2f, collect,  NULL},
    {0x30, 0x7e, doescape, &ground}
);

MAKESTATE(csi_entry, reset,
    {0x20, 0x2f, collect, &csi_intermediate},
    {0x3a, 0x3a, ignore,  &csi_ignore},
    {0x30, 0x39, param,   &csi_param},
    {0x3b, 0x3b, param,   &csi_param},
    {0x3c, 0x3f, collect, &csi_param},
    {0x40, 0x7e, docsi,   &ground}
);

MAKESTATE(csi_ignore, NULL,
    {0x20, 0x3f, ignore, NULL},
    {0x40, 0x7e, ignore, &ground}
);

MAKESTATE(csi_param, NULL,
    {0x30, 0x39, param,   NULL},
    {0x3b, 0x3b, param,   NULL},
    {0x3a, 0x3a, ignore,  &csi_ignore},
    {0x3c, 0x3f, ignore,  &csi_ignore},
    {0x20, 0x2f, collect, &csi_intermediate},
    {0x40, 0x7e, docsi,   &ground}
);

MAKESTATE(csi_intermediate, NULL,
    {0x20, 0x2f, collect, NULL},
    {0x30, 0x3f, ignore,  &csi_ignore},
    {0x40, 0x7e, docsi,   &ground}
);

MAKESTATE(osc_string, reset,
    {0x07, 0x07, doosc, &ground},
    {0x20, 0x7f, collectosc, NULL}
);
