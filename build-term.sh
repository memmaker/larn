#!/bin/sh
# Native terminal build (release): the curses shim drawn by port/be_term.c
# with ANSI escapes, no curses library. Run the game from a folder holding
# the larnfiles/ contents. sh build-term.sh [CC=...] [EXE=.exe] [EXTRA=...]
set -e
cd "$(dirname "$0")"
for a; do eval "${a%%=*}=\"\${a#*=}\""; done
${CC:-cc} -O2 -std=gnu99 -fcommon -DLARN_X11 -I. -Iport -w $EXTRA \
	*.c port/wcurses.c port/tiles.c port/rvip.c port/be_term.c $TLIBS -o larn-term$EXE
