# Larn (RL_M 26.4.0): handover

## Source and changes

- Base: **RL_M** by Gibbon, https://github.com/atsb/RL_M (master, commit 12cefcd), Larn 12.x
  maintained in C89. Licence: The Noah Licence (`docs/LICENSE.txt`, non-commercial).
- Our changes: `git diff` (game files, all `#ifdef LARN_X11`) plus the new `port/`, `play.sh`.
- Tiles: Amiga Larn tiles from larn.org's source, https://github.com/primeau/Larn `src/img/`
  (MIT, `port/amiga/LICENSE`). larn.org has **no sound effects**.

## Build and run

- `./play.sh` (builds `larn-x11` via `make -C port` if missing). Runs in `save/`, where it links
  the data files from `larnfiles/` and creates `larnopts` (colour on). Saves, scores, checkpoint
  in `save/`.
- The plain curses build still works (`make -f Makefile.macos`); none of the hooks are in it.
- Tiles: `python3 port/mktiles.py` → `port/tiles.rgba`, `port/tilemap.h`.

## Port (case R: curses shim with panes)

- `port/curses.h` + `wcurses.c`: in-memory 80×24 stdscr (found via `-Iport`). Routing:
  map rows 0-16 × cols 0-66 → Map pane (tiles); Status and Inventory panes are built from the
  game's data (`tiles.c`), not from the screen; message ring rows 20-23 → Messages pane.
  Mode hooks in the game: `wc_overlay()` in `cl_up()` (io.c) and `t_setup()` (inventory.c),
  `wc_dungeon()` at the end of `drawscreen()`, `clear()` = full-screen text,
  `wc_msgnew()` in `lprc()` when the ring starts a new line.
- `port/be_x11.c`: one X11 window per pane + override-redirect pop-up. Arrows/keypad send
  digits (Larn's `llgetch()` turns them into hjklyubn). Env: `LARN_SCALE`, `LARN_MAP` etc.,
  `LARN_DUMP=<file>` writes every pane as text on each refresh (use it for testing).
- `port/rvip.c`: explore (`x`), `<`/`>` walking, Enter menu (parsed from `larn.help` page 1),
  inventory with cursor + item menus, cursor list for every `whatitem()` prompt. Hooked in
  `parse()` (`rvip_command`) and `whatitem()` (main.c).
- Other game changes: `nap()` sleeps instead of spinning (idle loop kept a core at 100%);
  `yylex()` no longer drains typeahead in the X11 build (the item actions queue keys);
  `larn.help` lists `x`, Enter and the stair walking.

## Known limits

- Water, shore, lava and cooled lava have no Amiga tiles: coloured letters.
- The player tile is the Amiga original (a green block). The Desktop icon uses the Eye of Larn.
- Digit keys are movement at the command prompt (upstream), so repeat counts don't work.

## Web (RVIP step 7)

- Live: https://ruzzoli.de/roguelikes/larn/ · changes: https://github.com/memmaker/larn
  (remote `memmaker`), base atsb/RL_M @ 12cefcd.
- `sh web/build.sh` → `web/dist`, `sh web/deploy.sh`. Template: Umoria's web files.
- `port/be_web.c` replaces `be_x11.c`. Data (`larnfiles/`) preloaded to `/larn/data`; the
  game's cwd is the IDBFS mount `/larn/save` with symlinks to the data files, like `play.sh`.
- Saves: autosave (`savegame()`) at the command prompt on start, every 2 min and when the tab
  is hidden; `be_end()` (from `clearvt100()`) deletes the save unless the player pressed `S`.
- Sound: `SOUND("event")` calls in the game (larnfunc.h), Dubtrain samples via `web/sounds.py`.
- Upstream bug fixed on the way: `lcreat(NULL)` didn't send output back to the terminal, so
  after any mid-game save (checkpoint, autosave) the screen stopped updating.
