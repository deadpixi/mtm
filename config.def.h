/* The following definitions change the keys used to send commands
 * to mtm. In all cases, the value must be something that ncurses's
 * getch(3) function could return. The _KIND macros denote whether
 * the key is a special key (KEY_CODE_YES), or a regular (wide)
 * character (OK).
 */

/* The default command prefix key, when modified by cntrl.
 * This can be changed at runtime using the '-c' flag.
 */
#define COMMAND_KEY 'g'

/* The change focus keys. */
#define MOVE_UP         KEY_UP
#define MOVE_DOWN       KEY_DOWN
#define MOVE_RIGHT      KEY_RIGHT
#define MOVE_LEFT       KEY_LEFT

#define MOVE_UP_KIND    KEY_CODE_YES
#define MOVE_DOWN_KIND  KEY_CODE_YES
#define MOVE_RIGHT_KIND KEY_CODE_YES
#define MOVE_LEFT_KIND  KEY_CODE_YES

/* The split terminal keys. */
#define HSPLIT      L'h'
#define VSPLIT      L'v'
#define HSPLIT_KIND OK
#define VSPLIT_KIND OK

/* The delete terminal key. */
#define DELETE_NODE      L'w'
#define DELETE_NODE_KIND OK

/* The force redraw key. */
#define REDRAW      L'l'
#define REDRAW_KIND OK

/* The path for the wide-character curses library. */
#ifndef NCURSESW_INCLUDE_H
    #if defined(__APPLE__) || (defined(BSD) && !defined(__linux__))
        #define NCURSESW_INCLUDE_H <curses.h>
    #else
        #define NCURSESW_INCLUDE_H <ncursesw/curses.h>
    #endif
#endif
#include NCURSESW_INCLUDE_H

/* Includes needed to make forkpty(3) work. */
#ifndef FORKPTY_INCLUDE_H
    #if defined(__APPLE__) || (defined(BSD) && !defined(__linux__))
        #define FORKPTY_INCLUDE_H <util.h>
    #else
        #define FORKPTY_INCLUDE_H <pty.h>
    #endif
#endif
#include FORKPTY_INCLUDE_H
