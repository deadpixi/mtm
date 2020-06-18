#include <stdbool.h>

#include "config.h"

#define COLOR_MAX 256
static short pairs[COLOR_MAX][COLOR_MAX];
static short next = 1;

void
start_pairs(void)
{
   for (short i = 0; i < COLOR_MAX; i++)
      for (short j = 0; j < COLOR_MAX; j++)
         pairs[i][j] = -1;
}

short
mtm_alloc_pair(int fg, int bg)
{
#if USE_ALLOC_PAIR
   return alloc_pair(fg, bg);
#else
   if (next >= COLOR_PAIRS)
      return -1;
   if (fg >= COLOR_MAX || bg >= COLOR_MAX)
      return -1;
   if (pairs[fg][bg] == -1){
      pairs[fg][bg] = next++;
      init_pair(pairs[fg][bg], fg, bg);
   }
   return pairs[fg][bg];
#endif
}
