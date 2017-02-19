/* The following definitions change the keys used to send commands
 * to mtm. In all cases, the value must be something that ncurses's
 * getch(3) function could return.
 */

/* The default command prefix key, when modified by cntrl.
 * This can be changed at runtime using the '-c' flag.
 */
#define COMMAND_KEY 'g'

/* The change focus keys. */
#define MOVE_UP    KEY_UP
#define MOVE_DOWN  KEY_DOWN
#define MOVE_RIGHT KEY_RIGHT
#define MOVE_LEFT  KEY_LEFT

/* The split terminal keys. */
#define SPLIT_HORIZONTAL 'h'
#define SPLIT_VERTICAL 'v'

/* The delete terminal key. */
#define DELETE_NODE 'w'

/* The force redraw key. */
#define REDRAW 'l'
