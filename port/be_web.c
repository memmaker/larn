/* Browser frontend for the curses shim (RVIP step 7): web/larn.js draws the
 * panes (Module.ln); input waits with Asyncify. At the command prompt (the
 * game polls without waiting) the page can ask for an autosave. Larn deletes
 * the save when it restores it, so the autosave keeps the game alive across
 * reloads; be_end() removes it when the game ends without 'S'. */
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include "../larncons.h"
#include "../larndata.h"
#include "curses.h"

int savegame(char *);  /* diag.c */

EM_JS(void, js_init, (int p, int c, int r), { Module.ln.init(p, c, r); });
EM_JS(void, js_put, (int p, int y, int x, int ch, int t), { Module.ln.put(p, y, x, ch, t); });
EM_JS(void, js_cursor, (int p, int y, int x), { Module.ln.cursor(p, y, x); });
EM_JS(void, js_popup, (int r, int c), { Module.ln.popup(r, c); });
EM_JS(void, js_flush, (int lvl, int hy, int hx), { Module.ln.flush(lvl, hy, hx); });
EM_JS(int, js_key, (int at_cmd), { return Module.ln.key(at_cmd); });
EM_JS(void, js_prompt, (const char *s), { Module.ln.prompt(UTF8ToString(s)); });
void be_prompt(const char *s) { js_prompt(s); }
EM_JS(int, js_want_save, (void), { return Module.ln.wantSave(); });
EM_JS(void, js_sound, (const char *s), { Module.ln.sound(UTF8ToString(s)); });
EM_JS(void, js_end, (int saved), { Module.ln.end(saved); });

void be_init(int p, int cols, int rows) { js_init(p, cols, rows); }
void be_put(int p, int y, int x, chtype ch, int tile) { js_put(p, y, x, (int)ch, tile); }
void be_cursor(int p, int y, int x) { js_cursor(p, y, x); }
void be_popup(int rows, int cols) { js_popup(rows, cols); }
void be_sound(const char *event) { js_sound(event); }

/* Visible window (RVIP 5b): monsters in sight (Larn shows only the cells
 * around the player: 1, 2 with the Sword of Slashing, 3 with awareness) and
 * the objects drawn on the map */
EM_JS(void, js_invfg, (int y, const char *c), { Module.ln.invfg(y, UTF8ToString(c)); });
void be_invfg(int y, const char *css)
{
    static const char *last[64];
    if (y < 64 && last[y] != css) { last[y] = css; js_invfg(y, css); }
}
EM_JS(void, js_vis, (const char *s), { if (Module.ln.vis) Module.ln.vis(UTF8ToString(s)); });
static void send_visible(void)
{
    static char buf[4096];
    int n = 0, x, y, r = c[AWARENESS] ? 3 : (iven[c[WIELD]] == OHSWORD && ivenarg[c[WIELD]] >= 0) ? 2 : 1;
    if (c[BLINDCOUNT]) r = -1;
    for (y = playery - r; y <= playery + r; y++)
        for (x = playerx - r; x <= playerx + r; x++)
            if (x >= 0 && y >= 0 && x < MAXX && y < MAXY && mitem[x][y] && n < 3900)
                n += snprintf(buf + n, sizeof buf - n, "M%c%s\n", monstnamelist[mitem[x][y]], monster[mitem[x][y]].name);
    for (y = 0; y < MAXY; y++)
        for (x = 0; x < MAXX; x++)
            if ((know[x][y] & KNOWHERE) && item[x][y] && objnamelist[item[x][y]] > ' ' && n < 3900
                && !strchr("#.", objnamelist[item[x][y]]))
                n += snprintf(buf + n, sizeof buf - n, "I%c%s\t%s\n", objnamelist[item[x][y]], objectname[item[x][y]], wc_css(item[x][y]));
    buf[n] = 0;
    js_vis(buf);
}

void be_flush(void)
{
    send_visible();
    /* the player's cell: the page scrolls a zoomed-in map to keep it in view */
    js_flush(c[HP] > 0 ? level : -1, playery, playerx);
}

int be_getkey(int wait)
{
    for (;;) {
        int k = js_key(!wait);          /* ponytail: the command prompt is the one that polls */
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
