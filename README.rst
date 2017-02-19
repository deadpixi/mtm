Introduction
============

mtm is the Micro Terminal Multiplexer, a terminal multiplexer.

It has three major features/princples:

Simplicity
    There are three commands (change focus, split, close).  There are no
    modes, no dozens of commands, no crazy feature list.

Compatibility
    mtm emulates an existing, well-known terminal type.  That means it
    should work out of the box on essentially all termcap-based systems,
    even pretty old ones, without needing to install a new termcap entry.

Size
    mtm is less than 550 lines of C, including whitespace and comments.
    If you include the terminal emulation layer (which is `available
    separately`_ as a library), the total number of lines doesn't break 1050.

.. _`available separately`: https://github.com/deadpixi/libtmt

The Obiligatory Screenshot
--------------------------

.. image:: screenshot.png

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
- If you want to change the default keybindings, copy `config.def.h`
  to `config.h` and edit the copy. Otherwise the build process will
  use the default.
- Run `make`.
- Run `make install` if desired.

Compatibility
=============

One nice thing about mtm is that it emulates (accurately) an existing
terminal type that is widely supported: `mach` and/or `mach-color`.
This terminal emulation is actually implemented as a library, called
`libtmt`_, that you may find useful.

This means that mtm will work out-of-the-box on most systems, at least
terminal-emulation-wise.  There is generally no need to install a new
termcap definition (assuming your existing termcap database has entries for
`mach`/`mach-color`; most have for the past twenty years).

.. _`libtmt`: https://github.com/deadpixi/libtmt

Anything that uses termcap/terminfo or (n)curses should "just work"
with mtm.  mtm does not, however, support some features that some programs
want (and neither did the original `mach`/`mach-color` console).  The only
user-visible features that might be missed are terminal-title setting and
mouse support.  If you need those, mtm will not work for you, sorry.

Note that mtm also intentionally breaks compatibilty with the `mach`
console by rendering blinking text as bold instead.  This is because nobody
likes blinking text, and it can actually be dangerous for some people with
epilepsy and other conditions.

Usage
=====

Usage is simple::

    mtm [-m] [-c KEY] [-e MILLISECONDS]

The `-m` flag puts mtm in monochrome mode, if you need that.

The `-c` flag lets you specify a keyboard character to use as the "command
prefix" for mtm when modified with *control* (see below).  By default,
this is `g`.

The `-e` flag specifies how long mtm will wait after seeing an escape
character to see if it's the beginning of a special sequence.

Once inside mtm, things pretty much work like any other terminal.  However,
mtm lets you split up the terminal into multiple virtual terminals.

At any given moment, exactly one virtual terminal is *focused*.  It is
to this terminal that keyboad input is sent.  The focused terminal is
indicated by the location of the cursor.

The following commands are recognized in mtm, when preceded by the command
prefix (by defaul *ctrl-g*):

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

