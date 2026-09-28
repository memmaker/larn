/* Browser frontend for the curses shim (RVIP step 7): web/larn.js draws the
 * panes (Module.ln); input waits with Asyncify. At the command prompt (the
 * game polls without waiting) the page can ask for an autosave. Larn deletes
 * the save when it restores it, so the autosave keeps the game alive across
 * reloads; be_end() removes it when the game ends without 'S'. */
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../larncons.h"
#include "../larndata.h"
#include "curses.h"

int savegame(char *);  /* diag.c */

static const char *rowfg_sent[64]; /* be_rowfg: colour last sent per pop-up row */
EM_JS(void, js_init, (int p, int c, int r), { Module.ln.init(p, c, r); });
EM_JS(void, js_put, (int p, int y, int x, int ch, int t), { Module.ln.put(p, y, x, ch, t); });
EM_JS(void, js_extent, (int p, int c, int r), { Module.ln.extent(p, c, r); });
void be_extent(int p, int cols, int rows) { js_extent(p, cols, rows); }
EM_JS(void, js_cursor, (int p, int y, int x), { Module.ln.cursor(p, y, x); });
EM_JS(void, js_popup, (int r, int c), { Module.ln.popup(r, c); });
EM_JS(void, js_flush, (int lvl, int hy, int hx), { Module.ln.flush(lvl, hy, hx); });
EM_JS(int, js_key, (int at_cmd), { return Module.ln.key(at_cmd); });
EM_JS(void, js_prompt, (const char *s), { Module.ln.prompt(UTF8ToString(s)); });
void be_prompt(const char *s) { js_prompt(s); }
EM_JS(int, js_want_save, (void), { return Module.ln.wantSave(); });
EM_JS(void, js_sound, (const char *s), { Module.ln.sound(UTF8ToString(s)); });
/* Run report (roguelikes-index/server/CONTRACT.md): fire-and-forget GET,
   never throws, offline just fails silently. Negative ints are omitted. */
EM_JS(void, js_beacon, (const char *g, const char *ev, const char *name, const char *killer, int depth, int score, int turns, int lvl), {
    try {
        var p = [['g', UTF8ToString(g)], ['ev', UTF8ToString(ev)], ['name', name ? UTF8ToString(name) : ''],
                 ['killer', killer ? UTF8ToString(killer) : ''], ['depth', depth], ['score', score], ['turns', turns], ['lvl', lvl]];
        var q = p.filter(function (a) { return a[1] !== '' && !(a[1] < 0); })
                 .map(function (a) { return a[0] + '=' + encodeURIComponent(a[1]); }).join('&');
        if (window.RvipWM && RvipWM.report) RvipWM.report(q); else fetch('/roguelikes/beacon?' + q, { keepalive: true, mode: 'no-cors' }).catch(function () {});
    } catch (e) {}
});
void be_run_end(const char *ev, const char *killer)
{
    js_beacon("larn", ev, logname, killer, level, (int) (c[GOLD] + c[BANKACCOUNT]), (int) gtime, (int) c[LEVEL]);
}
EM_JS(void, js_end, (int saved), { Module.ln.end(saved); });

void be_init(int p, int cols, int rows) { js_init(p, cols, rows); }
void be_put(int p, int y, int x, chtype ch, int tile) { js_put(p, y, x, (int)ch, tile); }
void be_cursor(int p, int y, int x) { js_cursor(p, y, x); }
void be_popup(int rows, int cols) { memset(rowfg_sent, 0, sizeof rowfg_sent); js_popup(rows, cols); }
void be_sound(const char *event) { js_sound(event); }

/* Visible window (RVIP 5b): the monsters and objects drawn on the map. A
 * monster counts when the map shows its letter at its cell (Larn draws the
 * ones moving through explored cells, not only those next to the player),
 * the same test explore uses to stop; nearest first. */
EM_JS(void, js_invfg, (int y, const char *c, int t), { Module.ln.invfg(y, UTF8ToString(c), t); });
void be_invfg(int y, const char *css, int tile)
{
    static const char *last[64];
    static int lastt[64];
    if (y < 64 && (last[y] != css || lastt[y] != tile)) { last[y] = css; lastt[y] = tile; js_invfg(y, css, tile); }
}
EM_JS(void, js_rowfg, (int p, int y, const char *c), { Module.ln.rowfg(p, y, UTF8ToString(c)); });
void be_rowfg(int p, int y, const char *css)
{
    if (y < 64 && rowfg_sent[y] != css) { rowfg_sent[y] = css; js_rowfg(p, y, css); }
}
EM_JS(int, js_icons, (void), { return Module.ln.icons(); });
int be_icons(void) { return js_icons(); }
EM_JS(void, js_vis, (const char *s), { if (Module.ln.vis) Module.ln.vis(UTF8ToString(s)); });
static int mdist(int i)
{
    int dx = abs(i % MAXX - playerx), dy = abs(i / MAXX - playery);
    return dx > dy ? dx : dy;
}

static void send_visible(void)
{
    static char buf[4096];
    static int mon[MAXX * MAXY];
    int n = 0, x, y, i, j, nm = 0;
    for (y = 0; y < MAXY && !c[BLINDCOUNT]; y++)
        for (x = 0; x < MAXX; x++)
            if (mitem[x][y] && (int)(stdscr->c[y * stdscr->maxx + x] & A_CHARTEXT) == monstnamelist[mitem[x][y]])
                mon[nm++] = y * MAXX + x;
    for (i = 0; i < nm; i++)   /* nearest first (few monsters: insertion sort) */
        for (j = i; j > 0 && mdist(mon[j]) < mdist(mon[j - 1]); j--) { int t = mon[j]; mon[j] = mon[j - 1]; mon[j - 1] = t; }
    for (i = 0; i < nm && n < 3900; i++) {
        int m = mitem[mon[i] % MAXX][mon[i] / MAXX];
        n += snprintf(buf + n, sizeof buf - n, "M%c%s\t\t%d\n", monstnamelist[m], monster[m].name, wc_montile(m));
    }
    for (y = 0; y < MAXY; y++)
        for (x = 0; x < MAXX; x++)
            if ((know[x][y] & KNOWHERE) && item[x][y] && objnamelist[item[x][y]] > ' ' && n < 3900
                && !strchr("#.", objnamelist[item[x][y]]))
                n += snprintf(buf + n, sizeof buf - n, "I%c%s\t%s\t%d\n", objnamelist[item[x][y]], objectname[item[x][y]], wc_css(item[x][y]), wc_objtile(item[x][y]));
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
