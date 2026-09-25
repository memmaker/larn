/* Browser frontend for the curses shim (RVIP step 7): web/larn.js draws the
 * panes (Module.ln); input waits with Asyncify. At the command prompt (the
 * game polls without waiting) the page can ask for an autosave. Larn deletes
 * the save when it restores it, so the autosave keeps the game alive across
 * reloads; be_end() removes it when the game ends without 'S'. */
#include <emscripten.h>
#include <stdio.h>
#include "../larncons.h"
#include "../larndata.h"
#include "curses.h"

int savegame(char *);  /* diag.c */

EM_JS(void, js_init, (int p, int c, int r), { Module.ln.init(p, c, r); });
EM_JS(void, js_put, (int p, int y, int x, int ch, int t), { Module.ln.put(p, y, x, ch, t); });
EM_JS(void, js_cursor, (int p, int y, int x), { Module.ln.cursor(p, y, x); });
EM_JS(void, js_popup, (int r, int c), { Module.ln.popup(r, c); });
EM_JS(void, js_flush, (int lvl, int hy, int hx), { Module.ln.flush(lvl, hy, hx); });
EM_JS(int, js_key, (void), { return Module.ln.key(); });
EM_JS(int, js_want_save, (void), { return Module.ln.wantSave(); });
EM_JS(void, js_sound, (const char *s), { Module.ln.sound(UTF8ToString(s)); });
EM_JS(void, js_end, (int saved), { Module.ln.end(saved); });

void be_init(int p, int cols, int rows) { js_init(p, cols, rows); }
void be_put(int p, int y, int x, chtype ch, int tile) { js_put(p, y, x, (int)ch, tile); }
void be_cursor(int p, int y, int x) { js_cursor(p, y, x); }
void be_popup(int rows, int cols) { js_popup(rows, cols); }
void be_sound(const char *event) { js_sound(event); }

void be_flush(void)
{
    /* the player's cell: the page scrolls a zoomed-in map to keep it in view */
    js_flush(c[HP] > 0 ? level : -1, playery, playerx);
}

int be_getkey(int wait)
{
    for (;;) {
        int k = js_key();
        if (k >= 0) return k;
        if (!wait) { /* the command prompt polls: a safe moment to save */
            if (c[HP] > 0 && js_want_save()) savegame(savefilename);
            return -1; /* nap() yields to the browser */
        }
        emscripten_sleep(10);
    }
}

void be_end(void)
{
    if (!save_mode) { /* died or quit: no character to come back */
        remove(savefilename);
        remove(ckpfile);
    }
    js_end(save_mode);
}
