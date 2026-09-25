#!/bin/sh
# Larn, X11 frontend (curses shim, port/): tiled map top left, Status and
# Inventory to its right, Messages below (layout in port/be_x11.c; override
# with LARN_MAP/_STATUS/_MSG/_INV="x,y"). Saves, scores and options in save/.
cd "$(dirname "$0")" || exit 1
[ -x larn-x11 ] || make -C port >/dev/null || exit 1
export LARN_TILES="$PWD/port/tiles.rgba"
mkdir -p save && cd save || exit 1
for f in lfortune larnmaze holidays larn.help larn.clr; do
    [ -e "$f" ] || ln -s "../larnfiles/$f" "$f"
done
[ -f larnopts ] || sed 's/^color: off/color: on/' ../larnfiles/larnopts > larnopts
exec ../larn-x11 "$@"
