#include <stdbool.h>

#include "config.h"

#define COLOR_MAX 256
typedef struct PAIR PAIR;
struct PAIR{
    short fg, bg, cp;
};

static PAIR pairs[COLOR_MAX * COLOR_MAX];

void
start_pairs(void)
{
    for (int i = 0; i < COLOR_MAX * COLOR_MAX; i++)
        pairs[i].fg = pairs[i].bg = pairs[i].cp = -1;
}

short
mtm_alloc_pair(int fg, int bg)
{
#if USE_ALLOC_PAIR
    return alloc_pair(fg, bg);
#else
    if (fg >= COLOR_MAX || bg >= COLOR_MAX)
       return -1;
    for (int i = 0; i < COLOR_MAX * COLOR_MAX; i++){
        PAIR *p = pairs + i;
        if (p->cp == -1){
            p->fg = fg;
            p->bg = bg;
            p->cp = init_pair(i + 1, p->fg, p->bg) == OK? i + 1 : -1;
        }
        if (p->fg == fg && p->bg == bg && p->cp != -1)
            return p->cp;
    }
    return -1;
#endif
}
