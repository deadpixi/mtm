Introduction
============

mtm is the Micro Terminal Multiplexer, a terminal multiplexer.

It has four major features/princples:

Simplicity
    There are three commands (change focus, split, close).  There are no
    modes, no dozens of commands, no crazy feature list.

Compatibility
    mtm emulates a terminal similar to that described by `GNU screen`_.
    That means it should work out of the box on essentially all
    terminfo/termcap-based systems (even pretty old ones), without needing
    to install a new termcap entry.

Size
    mtm is small.
    The entire project is around 1500 lines of code.

Stability
    mtm is "finished" as it is now.  You don't need to worry about it
    changing on you unexpectedly.  The only changes that can happen at
    this point are:

    - Bug fixes.
    - Translation improvements.
    - Accessibility improvements.
    - Fixes to keep it working on modern OSes.

.. _`GNU screen`: https://www.gnu.org/software/screen/manual/html_node/Control-Sequences.html#Control-Sequences

Community
=========

Rob posts updates about mtm on Twitter at http://twitter.com/TheKingAdRob.

Installation
============
Installation and configuration is fairly simple:

- You need ncursesw.
  If you want to support terminal resizing, ncursesw needs to be
  compiled with its internal SIGWINCH handler; this is true for most
  precompiled distributions.  Other curses implementations might work,
  but have not been tested.
- Edit the variables at the top of the Makefile if you need to
  (you probably don't).
- If you want to change the default keybindings or other compile-time flags,
  copy `config.def.h` to `config.h` and edit the copy. Otherwise the build
  process will use the defaults.
- Run::

    make

  or::

    make CURSESLIB=curses

  whichever works for you.
- Run `make install` if desired.

Usage
=====

Usage is simple::

    mtm [-m] [-T NAME] [-t NAME] [-c KEY]

The `-m` flag enables mouse support.  Note that the host terminal obviously
must support mouse input.  Even terminals that support it natively might
not advertise such support in their default configuration; this can often
be fixed by using a different terminfo entry for the host terminal (see the
`-T` option).

The `-T` flag tells mtm to assume a different kind of host terminal.
This is useful when a terminal's default terminfo entry does not advertise
some desired capability. For example, the default `xterm` terminfo entry
does not advertise mouse motion tracking. If motion tracking is needed,
telling mtm to assume the `xterm-1002` or `xterm-1003` host terminal type
(which advertise motion tracking capability) would be useful.

The `-t` flag tells mtm what terminal type to advertise itself as.
Note that this doesn't change how mtm interprets control sequences; it
simply controls what the `TERM` environment variable is set to.

The `-c` flag lets you specify a keyboard character to use as the "command
prefix" for mtm when modified with *control* (see below).  By default,
this is `g`.

mtm also recognizes but ignores the `-u` and `-b` flags, for backwards
compatibility with older versions.

Once inside mtm, things pretty much work like any other terminal.  However,
mtm lets you split up the terminal into multiple virtual terminals.

At any given moment, exactly one virtual terminal is *focused*.  It is
to this terminal that keyboad input is sent.  The focused terminal is
indicated by the location of the cursor.

The following commands are recognized in mtm, when preceded by the command
prefix (by default *ctrl-g*):

Up/Down/Left/Right Arrow
    Focus the virtual terminal above/below/to the left of/to the right of
    the currently focused terminal.

h / v
    Split the focused virtual terminal in half horizontally/vertically,
    creating a new virtual terminal to the right/below.  The new virtual
    terminal is focused.

w
    Delete the focused virtual terminal.  Some other nearby virtual
    terminal will become focused if there are any left.  mtm will exit
    once all virtual terminals are closed.  Virtual terminals will also
    close if the program started inside them exits.

l
    Redraw the screen.

That's it.  There aren't dozens of commands, there are no modes, there's
nothing else to learn.

Screenshots
-----------

.. image:: screenshot.png
.. image:: screenshot2.png
.. image:: screenshot3.png
.. image:: screenshot4.png

Copyright and License
=====================

Copyright 2017 Rob King <jking@deadpixi.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

