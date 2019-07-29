#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/**** STATE DEFINITIONS
 * This was built by consulting the excellent state chart created by
 * Paul Flo Williams: http://vt100.net/emu/dec_ansi_parser
 * Please note that Williams does not (AFAIK) endorse this work.
 */
#define param               "param"
#define ignore              "ignore"
#define docontrol           "docontrol"
#define doescape            "doescape"
#define docsi               "docsi"
#define doprint             "doprint"
#define doosc               "doosc"
#define reset               "reset"
#define collect             "collect"
#define collectosc          "collectosc"
#define ground              "&ground"
#define escape              "&escape"
#define escape_intermediate "&escape_intermediate"
#define csi_entry           "&csi_entry"
#define csi_ignore          "&csi_ignore"
#define csi_param           "&csi_param"
#define csi_intermediate    "&csi_intermediate"
#define osc_string          "&osc_string"

typedef struct ACTION ACTION;
struct ACTION{
    wchar_t lo, hi;
    const char *cb;
    const char *next;
};

#define MAKESTATE(name, onentry, ...)                                       \
    static void name ## _dump(void)                                         \
    {                                                                       \
        static ACTION actions[]={                                           \
            {0x00, 0x00, ignore,    NULL},                                  \
            {0x7f, 0x7f, ignore,    NULL},                                  \
            {0x18, 0x18, docontrol, ground},                                \
            {0x1a, 0x1a, docontrol, ground},                                \
            {0x1b, 0x1b, ignore,    escape},                                \
            {0x01, 0x06, docontrol, NULL},                                  \
            {0x08, 0x17, docontrol, NULL},                                  \
            {0x19, 0x19, docontrol, NULL},                                  \
            {0x1c, 0x1f, docontrol, NULL},                                  \
            __VA_ARGS__ ,                                                   \
            {0x00, 0x00, NULL,      NULL}                                   \
        };                                                                  \
        printf("static STATE %s ={\n", #name);                              \
        printf("    %s ,\n", onentry ? onentry : "NULL");                   \
        printf("    {\n");                                                  \
        for (ACTION *a = actions; a->cb; a++){                              \
            for (wchar_t i = a->lo; i <= a->hi; i++)                        \
                printf("        [0x%02x] = {%s, %s},\n",                    \
                       (unsigned int)i, a->cb, a->next? a->next : "NULL");  \
        }                                                                   \
        printf("}\n};\n");                                                  \
    }                                                                       \

MAKESTATE(ground, NULL,
    {0x07, 0x07, docontrol, NULL},
    {0x20, 0x7e, doprint,   NULL}
)

MAKESTATE(escape, reset,
    {0x07, 0x07, docontrol, NULL},
    {0x21, 0x21, ignore,   osc_string},
    {0x20, 0x20, collect,  escape_intermediate},
    {0x22, 0x2f, collect,  escape_intermediate},
    {0x30, 0x4f, doescape, ground},
    {0x51, 0x57, doescape, ground},
    {0x59, 0x59, doescape, ground},
    {0x5a, 0x5a, doescape, ground},
    {0x5c, 0x5c, doescape, ground},
    {0x6b, 0x6b, ignore,   osc_string},
    {0x60, 0x6a, doescape, ground},
    {0x6c, 0x7e, doescape, ground},
    {0x5b, 0x5b, ignore,   csi_entry},
    {0x5d, 0x5d, ignore,   osc_string},
    {0x5e, 0x5e, ignore,   osc_string},
    {0x50, 0x50, ignore,   osc_string},
    {0x5f, 0x5f, ignore,   osc_string}
)

MAKESTATE(escape_intermediate, NULL,
    {0x07, 0x07, docontrol, NULL},
    {0x20, 0x2f, collect,  NULL},
    {0x30, 0x7e, doescape, ground}
)

MAKESTATE(csi_entry, reset,
    {0x07, 0x07, docontrol, NULL},
    {0x20, 0x2f, collect, csi_intermediate},
    {0x3a, 0x3a, ignore,  csi_ignore},
    {0x30, 0x39, param,   csi_param},
    {0x3b, 0x3b, param,   csi_param},
    {0x3c, 0x3f, collect, csi_param},
    {0x40, 0x7e, docsi,   ground}
)

MAKESTATE(csi_ignore, NULL,
    {0x07, 0x07, docontrol, NULL},
    {0x20, 0x3f, ignore, NULL},
    {0x40, 0x7e, ignore, ground}
)

MAKESTATE(csi_param, NULL,
    {0x07, 0x07, docontrol, NULL},
    {0x30, 0x39, param,   NULL},
    {0x3b, 0x3b, param,   NULL},
    {0x3a, 0x3a, ignore,  csi_ignore},
    {0x3c, 0x3f, ignore,  csi_ignore},
    {0x20, 0x2f, collect, csi_intermediate},
    {0x40, 0x7e, docsi,   ground}
)

MAKESTATE(csi_intermediate, NULL,
    {0x07, 0x07, docontrol, NULL},
    {0x20, 0x2f, collect, NULL},
    {0x30, 0x3f, ignore,  csi_ignore},
    {0x40, 0x7e, docsi,   ground}
)

MAKESTATE(osc_string, reset,
    {0x07, 0x07, doosc, ground},
    {0x20, 0x7e, collectosc, NULL}
)

int
main(void)
{
    ground_dump();
    escape_dump();
    escape_intermediate_dump();
    osc_string_dump();
    csi_intermediate_dump();
    csi_param_dump();
    csi_ignore_dump();
    csi_entry_dump();
    return EXIT_SUCCESS;
}

