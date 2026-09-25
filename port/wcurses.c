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
#define HIST 18          /* message history rows */
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

int wclear(WINDOW *w)
{
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

static void pflush(int i)
{
    WINDOW *p = pn[i];
    int y, x;
    for (y = 0; p && y < p->maxy; y++) {
        if (p->first[y] < 0) continue;
        for (x = p->first[y]; x <= p->last[y]; x++) be_put(i, y, x, p->c[y * p->maxx + x], -1);
        p->first[y] = p->last[y] = -1;
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

/* message history: scroll up, add one line */
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
        for (x = 0; sfx[x] && n + x < p->maxx; x++) set(p, HIST - 1, n + x, (unsigned char)sfx[x]);
        return;
    }
    reps = 1;
    strcpy(prev, r);
    for (y = 0; y < HIST - 1; y++)
        for (x = 0; x < p->maxx; x++) set(p, y, x, at(p, y + 1, x));
    for (x = 0; x < p->maxx; x++) set(p, HIST - 1, x, x < n ? at(stdscr, row, x) : ' ');
}

int wc_msgs; /* messages so far (explore stops on a new one) */

void wc_msgnew(void)
{
    wc_msgs++;
    if (live >= 0 && live != stdscr->cury) hist(live);
    live = stdscr->cury;
}

static void msg_refresh(void)
{
    int x;
    char r[256];
    for (x = 0; x < COLS; x++) set(pn[P_MSG], HIST, x, live >= 0 ? at(stdscr, live, x) : ' ');
    for (x = 0; x < COLS && x < 255; x++) r[x] = live >= 0 ? at(stdscr, live, x) & A_CHARTEXT : ' ';
    r[x] = 0;
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
    } else {
        untouch(pn[P_POP]);
    }
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) set(pn[P_POP], y - y0, x - x0, at(stdscr, y, x));
    if (cy >= y0 && cy <= y1 && cx >= x0 && cx <= x1) be_cursor(P_POP, cy - y0, cx - x0);
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
    be_cursor(-1, 0, 0);
    if (mode != M_FULL) msg_refresh();
    if (mode == M_DUNGEON) close_popup();
    else pop_refresh();
    if (mode != M_FULL) {
        if (mode == M_DUNGEON) map_refresh();
        wc_status(pn[P_STATUS]);
        wc_inv(pn[P_INV]);
    }
    if (mode != M_FULL && cy == live) be_cursor(P_MSG, HIST, cx);
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
