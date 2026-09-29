# Larn (RL_M 26.4.0): handover

## Source and changes

- Base: **RL_M** by Gibbon, https://github.com/atsb/RL_M (master, commit 12cefcd), Larn 12.x
  maintained in C89. Licence: The Noah Licence (`docs/LICENSE.txt`, non-commercial).
- Our changes: `git diff` against the base (game files, `#ifdef LARN_X11`) plus `port/`, `web/`.
- Tiles: Amiga Larn tiles from larn.org's source, https://github.com/primeau/Larn `src/img/`
  (MIT, `port/amiga/LICENSE`). larn.org has **no sound effects**.

## Web (main target)

- Live: https://ruzzoli.de/roguelikes/larn/ · repo: https://github.com/memmaker/larn (remote `memmaker`).
- `sh web/build.sh` → `web/dist`, `sh web/deploy.sh`. Shared `rvip-wm.js`, `rvip-app.js`,
  `rvip-sound.js` come from `~/Games/rvip-tools` at build time (no copies here).
- `port/be_web.c` is the backend. Data (`larnfiles/`) preloaded to `/larn/data`; the game's cwd
  is the IDBFS mount `/larn/save` with symlinks to the data files.
- Saves: autosave (`savegame()`) at the command prompt on start, every 2 min and when the tab is
  hidden; `be_end()` (from `clearvt100()`) deletes the save unless the player pressed `S`.
- Sound: `SOUND("event")` calls in the game (larnfunc.h), Dubtrain samples via `web/sounds.py`.
- Text windows are HTML lines (`be_line`/`be_rows`, row colour/icon via `wc_rowattr`); only the
  map is a canvas. Prompt line: `be_prompt(r)` from `msg_refresh()` (wcurses.c); no command-prompt
  flag exists, so `be_web.c` passes `!wait`.
- Stage 9 beacon (graveyard + leaderboard) is wired in.

## Port (case R: curses shim with panes)

- `port/curses.h` + `wcurses.c`: in-memory 80×24 stdscr (found via `-Iport`). Map rows 0-16 ×
  cols 0-66 → Map pane; Status and Inventory built from game data (`tiles.c`); message ring rows
  20-23 → Messages. Mode hooks: `wc_overlay()` in `cl_up()` (io.c) and `t_setup()`
  (inventory.c), `wc_dungeon()` at the end of `drawscreen()`, `wc_msgnew()` in `lprc()`.
- `port/rvip.c`: explore (`x`), `<`/`>` walking, Enter menu (parsed from `larn.help` page 1),
  inventory + item menus, cursor list for every `whatitem()` prompt. Hooked in `parse()`
  (`rvip_command`) and `whatitem()` (main.c).
- Direction prompts (`dirsub()`) read via `ttgetdir()` (io.c): arrows/keypad/digits → hjklyubn.
- Other game changes: `nap()` sleeps instead of spinning; `yylex()` doesn't drain typeahead in
  the port build; upstream bug fixed: `lcreat(NULL)` didn't send output back to the terminal.
- Tiles: `python3 port/mktiles.py` → `port/tiles.rgba`, `port/tilemap.h`.

## Native builds (testing / releases)

- X11: `./play.sh` (builds `larn-x11` via `make -C port`); `LARN_DUMP=<file>` writes every pane
  as text on each refresh — handy for testing.
- Terminal: `build-term.sh` (`port/be_term.c`, ANSI escapes); `.github/workflows/release.yml`
  builds it on a `v*` tag.
- Plain curses upstream build: `make -f Makefile.macos` (no hooks).

## Known limits

- Water, shore, lava and cooled lava have no Amiga tiles: coloured letters.
- The player tile is the Amiga original (a green block).
- Digit keys are movement at the command prompt (upstream), so repeat counts don't work.
