#!/bin/sh
# Build Larn for the browser (Emscripten + Asyncify) into web/dist;
# deploy with web/deploy.sh. Run with sh (zsh doesn't split lists).
set -e
cd "$(dirname "$0")/.."
OUT=web/dist
rm -rf "$OUT" && mkdir -p "$OUT"
[ -f port/tiles.png ] || python3 port/mktiles.py
# -DLARN_X11 selects the curses shim (port/curses.h); be_web.c replaces be_x11.c
emcc -O2 -std=gnu99 -fcommon -DLARN_X11 -I. -Iport -w \
	*.c port/wcurses.c port/tiles.c port/rvip.c port/be_web.c \
	-o "$OUT/larn-core.js" \
	-sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sSTACK_SIZE=1048576 \
	-sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=32MB \
	-sEXPORTED_FUNCTIONS=_main \
	-sEXPORTED_RUNTIME_METHODS=FS,IDBFS,HEAPU8,addRunDependency,removeRunDependency \
	-sFORCE_FILESYSTEM -lidbfs.js -sENVIRONMENT=web \
	--preload-file larnfiles@/larn/data
cp web/index.html "$HOME/Games/rvip-tools/web/rvip-wm.js" web/larn.js port/tiles.png "$OUT/"
# sound effects (events raised by SOUND() in the game) and the town music
mkdir -p "$OUT/sound" "$OUT/music"
python3 web/sounds.py "$OUT/sound"
cp ~/Projects/heavenAndHell/files/mods/heavenandhell/music/new_town.ogg "$OUT/music/"
python3 web/make-help.py > "$OUT/help.html"
ls -la "$OUT"
