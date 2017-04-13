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
#ifndef VTC_H
#define VTC_H

#include <stdlib.h>
#include <wchar.h>

/**** CONFIGURATION */
#ifndef VTPARSER_BAD_CHAR
    #ifdef __STDC_ISO_10646__
        #define VTPARSER_BAD_CHAR ((wchar_t)0xfffd)
    #else
        #define VTPARSER_BAD_CHAR L'?'
    #endif
#endif

/**** DATA TYPES */
typedef struct VTPARSER VTPARSER;

typedef void (*VTCALLBACK)(VTPARSER *v, void *p, wchar_t w, wchar_t iw,
                           int argc, int *argv);

typedef enum{
    VTPARSER_CONTROL,
    VTPARSER_ESCAPE,
    VTPARSER_CSI,
    VTPARSER_PRINT
} vtparser_event_t;

/**** FUNCTIONS */
VTPARSER *
vtparser_open(void *p);

void
vtparser_close(VTPARSER *vp);

VTCALLBACK
vtparser_onevent(VTPARSER *vp, vtparser_event_t t, wchar_t w, VTCALLBACK cb);

void
vtparser_write(VTPARSER *vp, const char *s, size_t n);

#endif
