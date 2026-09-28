/* In-memory curses for Larn, routed to Angband-style panes.
 *
 * Larn draws everything on one 80x24 screen: map (rows 0-16, cols 0-66),
 * effects column (cols 69-79), status lines (17-19) and a four-line
 * message ring (20-23). The frontend shows these as separate windows:
 *   Map        the map area, tiled (tile_for)
 *   Status     built from the game's data (wc_status)
 *   Messages   history + the message being written
 *   Inventory  built from the pack (wc_inv)
 *   Pop-up     anything else (inventory lists, help, stores, spells),
 *              sized to its content.
 * Mode comes from hooks in the game:
 *   clear()          -> full screen text: pop-up = all non-blank text
 *   wc_overlay()     -> text over the map: pop-up = cells that changed
 *   wc_dungeon()     -> the map was drawn again
 *   wc_msgnew()      -> a message line starts: the last one goes to history */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "curses.h"

WINDOW *stdscr, *curscr;
int LINES = 24, COLS = 80;

enum { M_DUNGEON, M_OVERLAY, M_FULL };
static int mode = M_FULL;
static WINDOW *base; /* the screen when the overlay started */
static WINDOW *pn[NPANES];
static chtype shown[17 * 67]; /* what the Map pane has, per cell */
static int shown_tile[17 * 67];
static int nodelay_on, live = -1;

#define MAP_H 17
#define MAP_W 67
#define MSG_Y 20
#ifdef __EMSCRIPTEN__
#define HIST 100         /* message history rows: the page's Messages window scrolls */
#else
#define HIST 18          /* message history rows */
#endif
#define ST_W 42
#define ST_H 15
#define INV_W 42
#define INV_H 31

WINDOW *newwin(int rows, int cols, int y, int x)
{
    WINDOW *w = calloc(1, sizeof(WINDOW));
    int i;
    (void)y; (void)x;
    if (rows == 0) rows = LINES;
    if (cols == 0) cols = COLS;
    w->maxy = rows;
    w->maxx = cols;
    w->c = malloc(sizeof(chtype) * rows * cols);
    w->first = malloc(sizeof(short) * rows);
    w->last = malloc(sizeof(short) * rows);
    for (i = 0; i < rows * cols; i++) w->c[i] = ' ';
    for (i = 0; i < rows; i++) { w->first[i] = 0; w->last[i] = (short)(cols - 1); }
    return w;
}

static void delwin(WINDOW *w)
{
    if (!w) return;
    free(w->c); free(w->first); free(w->last); free(w);
}

WINDOW *initscr(void)
{
    if (!stdscr) {
        curscr = newwin(LINES, COLS, 0, 0);
        stdscr = newwin(LINES, COLS, 0, 0);
        base = newwin(LINES, COLS, 0, 0);
        memset(shown, 0xff, sizeof shown);
        pn[P_STATUS] = newwin(ST_H, ST_W, 0, 0);
        pn[P_MSG] = newwin(HIST + 1, COLS, 0, 0);
        pn[P_INV] = newwin(INV_H, INV_W, 0, 0);
        be_init(P_MAP, MAP_W, MAP_H);
        be_init(P_STATUS, ST_W, ST_H);
        be_init(P_MSG, COLS, HIST + 1);
        be_init(P_INV, INV_W, INV_H);
    }
    return stdscr;
}

int endwin(void) { return OK; }
int has_colors(void) { return TRUE; }
int curs_set(int n) { (void)n; return OK; }
int nodelay(WINDOW *w, int b) { (void)w; nodelay_on = b; return OK; }

static void touch(WINDOW *w, int y, int x)
{
    if (w->first[y] < 0 || x < w->first[y]) w->first[y] = (short)x;
    if (x > w->last[y]) w->last[y] = (short)x;
}

static void untouch(WINDOW *w)
{
    int y;
    for (y = 0; y < w->maxy; y++) w->first[y] = w->last[y] = -1;
}

int wmove(WINDOW *w, int y, int x)
{
    if (y < 0 || x < 0 || y >= w->maxy || x >= w->maxx) return ERR;
    w->cury = y;
    w->curx = x;
    return OK;
}

static void set(WINDOW *w, int y, int x, chtype ch)
{
    if (y < 0 || x < 0 || y >= w->maxy || x >= w->maxx || w->c[y * w->maxx + x] == ch) return;
    w->c[y * w->maxx + x] = ch;
    touch(w, y, x);
}

int waddch(WINDOW *w, chtype ch)
{
    int c = (int)(ch & A_CHARTEXT);
    if (c == '\n') {
        wclrtoeol(w);
        if (w->cury + 1 < w->maxy) { w->cury++; w->curx = 0; }
        return OK;
    }
    if (c == '\r') { w->curx = 0; return OK; }
    if (c == '\b') { if (w->curx > 0) w->curx--; return OK; }
    set(w, w->cury, w->curx, (ch & ~A_CHARTEXT) | w->attr | (chtype)c);
    if (++w->curx >= w->maxx) {
        if (w->cury + 1 >= w->maxy) { w->curx = w->maxx - 1; return ERR; }
        w->cury++;
        w->curx = 0;
    }
    return OK;
}

int waddstr(WINDOW *w, const char *s)
{
    while (*s)
        if (waddch(w, (unsigned char)*s++) == ERR) return ERR;
    return OK;
}

int wclrtoeol(WINDOW *w)
{
    int x;
    for (x = w->curx; x < w->maxx; x++) set(w, w->cury, x, ' ');
    return OK;
}

int wclrtobot(WINDOW *w)
{
    int cy = w->cury, cx = w->curx, y, x;
    wclrtoeol(w);
    for (y = cy + 1; y < w->maxy; y++)
        for (x = 0; x < w->maxx; x++) set(w, y, x, ' ');
    w->cury = cy;
    w->curx = cx;
    return OK;
}

static const char *rowfg[64]; /* stdscr rows: colour set by the game (wc_rowfg) */
void wc_rowfg(WINDOW *w, int y, const char *css)
{
    if (w == stdscr && y >= 0 && y < 64) rowfg[y] = css && *css ? css : NULL;
}

int wclear(WINDOW *w)
{
    if (w == stdscr) memset(rowfg, 0, sizeof rowfg);
    w->cury = w->curx = 0;
    wclrtobot(w);
    if (w == stdscr) mode = M_FULL;
    return OK;
}

void wc_dungeon(void) { mode = M_DUNGEON; }

void wc_overlay(void)
{
    if (mode != M_DUNGEON) return;
    memcpy(base->c, stdscr->c, sizeof(chtype) * LINES * COLS);
    mode = M_OVERLAY;
}

static chtype at(WINDOW *w, int y, int x) { return w->c[y * w->maxx + x]; }

/* ---- panes ---- */

static int pop_h, pop_w;

/* Text panes go out as whole lines (RVIP W0 rules 5, 6): each changed row
 * once, trimmed, standout between \x01 and \x02, with the row's colour and
 * icon tile; and the rows in use (to the last non-blank row or the cursor),
 * so the page shows no empty lines at the bottom. */
#define RMAX 128
static const char *rcss[NPANES][RMAX];
static int rtile[NPANES][RMAX], rows_sent[NPANES], cur_p = -1, cur_y;

void wc_rowattr(int p, int y, const char *css, int tile)
{
    WINDOW *w = pn[p];
    if (!css) css = "";
    if (!w || y < 0 || y >= w->maxy || y >= RMAX) return;
    if (rcss[p][y] && !strcmp(rcss[p][y], css) && rtile[p][y] == tile) return;
    rcss[p][y] = css; rtile[p][y] = tile;
    touch(w, y, 0);
}

static void cursor(int p, int y, int x) { cur_p = p; cur_y = y; be_cursor(p, y, x); }

static int blank(chtype ch) { return (ch & A_CHARTEXT) == ' ' && !(ch & A_STANDOUT); }

/* a cell's colour run "\x05#rrggbb" ("\x05*#rrggbb": bold): curses colours
 * 0-7, +8 bold (as the map's palette in larn.js); bold without a colour
 * keeps the row's colour, bold weight */
static const char *pal[16] = { "#000000", "#cd3131", "#0dbc79", "#e5e510", "#4c7eff", "#bc3fbc", "#11a8cd", "#d7d7d7",
    "#666666", "#f14c4c", "#23d18b", "#f5f543", "#6ea0ff", "#d670d6", "#29b8db", "#ffffff" };
static int run(chtype ch, const char *row, char *out)
{
    int bold = (ch & A_BOLD) != 0, col = ch & 0x800 ? (ch >> 8) & 7 : 0;
    const char *css;
    if (!col && !bold) return 0;
    css = col ? pal[col + 8 * bold] : *row ? row : pal[15];
    return sprintf(out, "\x05%s%s", bold ? "*" : "", css);
}

/* native backends: a text pane cell with its attributes (be_line draws from these) */
chtype wc_cell(int p, int y, int x)
{
    WINDOW *w = pn[p];
    return w && y >= 0 && y < w->maxy && x >= 0 && x < w->maxx ? w->c[y * w->maxx + x] : ' ';
}

static void pflush(int i)
{
    WINDOW *p = pn[i];
    int y, x, used = 0;
    if (!p) return;
    for (y = 0; y < p->maxy; y++)
        for (x = 0; x < p->maxx; x++)
            if (!blank(p->c[y * p->maxx + x])) used = y + 1;
    if (cur_p == i && cur_y >= used) used = cur_y + 1;
    if (used != rows_sent[i]) be_rows(i, rows_sent[i] = used);
    for (y = 0; y < p->maxy; y++) {
        char buf[12 * 512 + 4], r[16], cr[16] = "";
        const char *row = y < RMAX && rcss[i][y] ? rcss[i][y] : "";
        int n = 0, so = 0, end = p->maxx;
        if (p->first[y] < 0) continue;
        p->first[y] = p->last[y] = -1;
        while (end > 0 && blank(p->c[y * p->maxx + end - 1])) end--;
        for (x = 0; x < end && x < 512; x++) {
            chtype ch = p->c[y * p->maxx + x];
            int c = ch & A_CHARTEXT, s = (ch & A_STANDOUT) != 0;
            r[run(ch, row, r)] = 0;
            if (c == ' ' && !s) strcpy(r, cr);       /* a blank doesn't break a run */
            if (s != so || strcmp(r, cr)) {
                if (*cr) buf[n++] = 6;
                if (s != so) buf[n++] = (so = s) ? 1 : 2;
                n += sprintf(buf + n, "%s", r);
                strcpy(cr, r);
            }
            buf[n++] = c < 32 || c > 126 ? ' ' : c;
        }
        if (*cr) buf[n++] = 6;
        if (so) buf[n++] = 2;
        buf[n] = 0;
        be_line(i, y, buf, row, y < RMAX && rcss[i][y] ? rtile[i][y] : -1);
    }
}

static void close_popup(void)
{
    if (pop_h) be_popup(0, 0);
    pop_h = pop_w = 0;
}

static void map_refresh(void)
{
    int y, x;
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++) {
            int i = y * MAP_W + x;
            chtype ch = at(stdscr, y, x);
            int t = tile_for(y, x, ch);
            if (shown[i] == ch && shown_tile[i] == t) continue;
            shown[i] = ch;
            shown_tile[i] = t;
            be_put(P_MAP, y, x, ch, t);
        }
}

/* message history fills from the top (nhist rows in use, the live row
 * below them); once full it scrolls up */
static int nhist;
static void hist(int row)
{
    WINDOW *p = pn[P_MSG];
    int y, x, n = COLS;
    static char prev[512];
    static int reps;
    char r[512], sfx[16];
    while (n > 0 && (at(stdscr, row, n - 1) & A_CHARTEXT) == ' ') n--;
    if (n == 0) return;
    for (x = 0; x < n && x < 511; x++) r[x] = at(stdscr, row, x) & A_CHARTEXT;
    r[x] = 0;
    /* a repeat of the newest line: "line (xN)" in its row */
    if (!strcmp(r, prev)) {
        snprintf(sfx, sizeof sfx, " (x%d)", ++reps);
        for (x = 0; sfx[x] && n + x < p->maxx; x++) set(p, nhist - 1, n + x, (unsigned char)sfx[x]);
        return;
    }
    reps = 1;
    strcpy(prev, r);
    if (nhist == HIST) {
        for (y = 0; y < HIST - 1; y++)
            for (x = 0; x < p->maxx; x++) set(p, y, x, at(p, y + 1, x));
        nhist--;
    }
    for (x = 0; x < p->maxx; x++) set(p, nhist, x, x < n ? at(stdscr, row, x) : ' ');
    nhist++;
}

int wc_msgs; /* messages so far (explore stops on a new one) */

void wc_msgnew(void)
{
    wc_msgs++;
    if (live >= 0 && live != stdscr->cury) hist(live);
    live = stdscr->cury;
}

static int prompt_msgs = -1; /* wc_msgs when the prompt line was last sent */

static void msg_refresh(void)
{
    int x;
    char r[256];
    int y;
    for (y = nhist; y <= HIST; y++)
        for (x = 0; x < COLS; x++) set(pn[P_MSG], y, x, y == nhist && live >= 0 ? at(stdscr, live, x) : ' ');
    for (x = 0; x < COLS && x < 255; x++) r[x] = live >= 0 ? at(stdscr, live, x) & A_CHARTEXT : ' ';
    r[x] = 0;
    /* a new message shows the prompt line again, even with the same text as
     * the one a key hid ("Not with a monster in view." on the second x) */
    if (prompt_msgs != wc_msgs) { prompt_msgs = wc_msgs; be_prompt(""); }
    be_prompt(r);                   /* the prompt line over the map */
}

/* Pop-up: bounding box of the text that isn't the game screen underneath. */
static void pop_refresh(void)
{
    int y0 = 99, y1 = -1, x0 = 99, x1 = -1, y, x;
    int cy = stdscr->cury, cx = stdscr->curx;
    int rows = mode == M_OVERLAY ? MSG_Y : LINES;
    for (y = 0; y < rows; y++)
        for (x = 0; x < COLS; x++) {
            chtype ch = at(stdscr, y, x);
            if ((ch & A_CHARTEXT) == ' ' && !(ch & A_STANDOUT)) continue;
            if (mode == M_OVERLAY && ch == at(base, y, x)) continue;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
        }
    if (y1 < 0) { close_popup(); return; }
    /* room for the cursor of a prompt ("Which one? _") */
    if (cy >= y0 && cy <= y1 && cx > x1 && cx < COLS) x1 = cx;
    if (y1 - y0 + 1 != pop_h || x1 - x0 + 1 != pop_w) {
        pop_h = y1 - y0 + 1;
        pop_w = x1 - x0 + 1;
        be_popup(pop_h, pop_w);
        delwin(pn[P_POP]);
        pn[P_POP] = newwin(pop_h, pop_w, 0, 0);
        memset(rcss[P_POP], 0, sizeof rcss[P_POP]);
        rows_sent[P_POP] = 0;
    } else {
        untouch(pn[P_POP]);
    }
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) set(pn[P_POP], y - y0, x - x0, at(stdscr, y, x));
        wc_rowattr(P_POP, y - y0, rowfg[y], -1);
    }
    if (cy >= y0 && cy <= y1 && cx >= x0 && cx <= x1) cursor(P_POP, cy - y0, cx - x0);
}

static void dump(FILE *f, const char *name, WINDOW *p)
{
    int y, x;
    fprintf(f, "== %s\n", name);
    for (y = 0; p && y < p->maxy; y++) {
        for (x = 0; x < p->maxx; x++) fputc((int)(at(p, y, x) & A_CHARTEXT), f);
        fputc('\n', f);
    }
}

int wrefresh(WINDOW *w)
{
    int cy = stdscr->cury, cx = stdscr->curx, i;
    const char *d;
    if (w != stdscr) return OK;
    cursor(-1, 0, 0);
    if (mode != M_FULL) msg_refresh();
    if (mode == M_DUNGEON) close_popup();
    else pop_refresh();
    if (mode != M_FULL) {
        if (mode == M_DUNGEON) map_refresh();
        wc_status(pn[P_STATUS]);
        wc_inv(pn[P_INV]);
    }
    if (mode != M_FULL && cy == live) cursor(P_MSG, nhist, cx);
    untouch(stdscr);
    for (i = P_STATUS; i < NPANES; i++)
        if (i != P_POP || pop_h) pflush(i);
    be_flush();
    if ((d = getenv("LARN_DUMP"))) { /* testing: panes as text */
        FILE *f = fopen(d, "w");
        if (f) {
            fprintf(f, "mode %d cursor %d,%d\n", mode, cy, cx);
            dump(f, "SCREEN", stdscr);
            dump(f, "STATUS", pn[P_STATUS]);
            dump(f, "MSG", pn[P_MSG]);
            dump(f, "INV", pn[P_INV]);
            dump(f, "POP", pop_h ? pn[P_POP] : NULL);
            fclose(f);
        }
    }
    return OK;
}

static int pushback = -1;
static char queue[64]; /* keys fed before the keyboard (item actions) */

void wc_push(const char *keys)
{
    size_t n = strlen(queue);
    snprintf(queue + n, sizeof queue - n, "%s", keys);
}

int wgetch(WINDOW *w)
{
    int k = pushback;
    if (queue[0]) {
        k = (unsigned char)queue[0];
        memmove(queue, queue + 1, strlen(queue));
        return k;
    }
    wrefresh(w);
    pushback = -1;
    if (k >= 0) return k;
    k = be_getkey(!nodelay_on);
    return k < 0 ? ERR : k;
}

int wc_kbhit(void)
{
    if (pushback < 0) pushback = be_getkey(0);
    return pushback >= 0;
}
