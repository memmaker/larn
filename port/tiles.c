/* Map cell -> Amiga Larn tile (port/tilemap.h), and the Status and
 * Inventory panes, built from the game's own data. The character on screen
 * must still match what the game would draw there, so blindness, unknown
 * cells and invisible monsters stay what the game shows. */
#include <stdio.h>
#include <string.h>
#include "../larncons.h"
#include "../larndata.h"
#include "curses.h"
#include "tilemap.h"

const char *bot_effect(int i); /* display.c */
#define MAXINVEN 26         /* inventory.c */

static int is_wall(int x, int y)
{
    int o;
    if (x < 0 || y < 0 || x >= MAXX || y >= MAXY) return 0;
    o = item[x][y];
    return o == OWALL || o == OINNERWALL || o == OCLOSEDDOOR || o == OOPENDOOR;
}

int tile_for(int y, int x, chtype ch)
{
    int c = (int)(ch & A_CHARTEXT), m, o;
    if (x >= MAXX || y >= MAXY) return -1;
    if (x == playerx && y == playery && (ch & A_STANDOUT) && c == ' ') return PLAYER_TILE;
    if (c == ' ' || !know[x][y]) return -1;
    m = mitem[x][y];
    if (m > 0 && m < (int)(sizeof mon_tile / sizeof mon_tile[0]) && c == monstnamelist[m]) return mon_tile[m];
    o = item[x][y];
    if (o < 0 || o >= (int)(sizeof obj_tile / sizeof obj_tile[0]) || c != objnamelist[o]) return -1;
    if (obj_tile[o] == -2)
        return wall_tile[(is_wall(x, y - 1) ? 1 : 0) | (is_wall(x + 1, y) ? 2 : 0) | (is_wall(x, y + 1) ? 4 : 0) |
                         (is_wall(x - 1, y) ? 8 : 0)];
    return obj_tile[o];
}

static int ln;

static const char *trim(const char *s)
{
    while (*s == ' ') s++;
    return s;
}

static void line(WINDOW *w, chtype attr, const char *s)
{
    if (ln >= w->maxy) return;
    wmove(w, ln++, 0);
    w->attr = attr;
    while (*s && w->curx < w->maxx - 1) waddch(w, (unsigned char)*s++);
    w->attr = 0;
    wclrtoeol(w);
}

void wc_status(WINDOW *w)
{
    char b[128];
    const char *e;
    int i, x = 0;
    ln = 0;
    snprintf(b, sizeof b, "%s, %s", logname, c[LEVEL] > 0 ? trim(classname[c[LEVEL] - 1]) : "");
    line(w, A_BOLD, b);
    snprintf(b, sizeof b, "Level %ld   Exp %ld", c[LEVEL], c[EXPERIENCE]);
    line(w, 0, b);
    snprintf(b, sizeof b, "HP %ld(%ld)   Spells %ld(%ld)", c[HP], c[HPMAX], c[SPELLS], c[SPELLMAX]);
    line(w, c[HP] * 4 < c[HPMAX] ? COLOR_PAIR(COLOR_RED) | A_BOLD : 0, b);
    snprintf(b, sizeof b, "AC %ld   WC %ld", c[AC], c[WCLASS]);
    line(w, 0, b);
    snprintf(b, sizeof b, "STR %ld  INT %ld  WIS %ld", c[STRENGTH] + c[STREXTRA], c[INTELLIGENCE], c[WISDOM]);
    line(w, 0, b);
    snprintf(b, sizeof b, "CON %ld  DEX %ld  CHA %ld", c[CONSTITUTION], c[DEXTERITY], c[CHARISMA]);
    line(w, 0, b);
    snprintf(b, sizeof b, "Gold %ld", c[GOLD]);
    line(w, COLOR_PAIR(COLOR_YELLOW), b);
    snprintf(b, sizeof b, "Dungeon: %s", trim(levelname[level]));
    line(w, 0, b);
    snprintf(b, sizeof b, "Time: %ld mobuls left", (TIMELIMIT - gtime) / 100);
    line(w, 0, b);
    line(w, 0, "");
    /* active effects, as many per line as fit */
    b[0] = 0;
    for (i = 0; (e = bot_effect(i)); i++) {
        if (!*e) continue;
        if (x + (int)strlen(e) + 2 > w->maxx - 1) {
            line(w, COLOR_PAIR(COLOR_CYAN), b);
            b[0] = 0;
            x = 0;
        }
        x += snprintf(b + x, sizeof b - x, "%s%s", x ? ", " : "", e);
    }
    if (x) line(w, COLOR_PAIR(COLOR_CYAN), b);
    while (ln < w->maxy) line(w, 0, "");
}

/* Same text as inventoryline_print() (inventory.c) */
void item_name(char *b, size_t n, int i)
{
    int o = iven[i], a = ivenarg[i];
    int k = snprintf(b, n, "%c) %s", 'a' + i, objectname[o]);
    if (o == OPOTION && potionname[a][0])
        k += snprintf(b + k, n - k, " of%s", potionname[a]);
    else if (o == OSCROLL && scrollname[a][0])
        k += snprintf(b + k, n - k, " of%s", scrollname[a]);
    else if (a > 0)
        k += snprintf(b + k, n - k, " + %d", a);
    else if (a < 0)
        k += snprintf(b + k, n - k, " %d", a);
    if (c[WIELD] == i) k += snprintf(b + k, n - k, " (in hand)");
    if (c[WEAR] == i || c[SHIELD] == i) snprintf(b + k, n - k, " (worn)");
}

/* Angband's colour for an object id (RVIP W0: colours come from the game) */
const char *wc_css(int o)
{
    switch (o) {
    case OPOTION: case OWATER: return "#40a0ff";
    case OSCROLL: return "#ffffff";
    case OBOOK: return "#60e0e0";
    case OAMULET: case OORBOFDRAGON: case OSPIRITSCARAB: case OCUBEofUNDEAD: case ONOTHEFT: case OSPHTAILSMAN: return "#ff9000";
    case ORING: case OSTUDLEATHER: case OSPLINT: case OPLATEARMOR: case OSSPLATE: case OSHIELD: case OELVENCHAIN:
    case OPLATE: case OCHAIN: case OLEATHER: return "#a07040";
    case ORINGOFEXTRA: case OREGENRING: case OPROTRING: case OENERGYRING: case ODEXRING: case OSTRRING:
    case OCLEVERRING: case ODAMRING: case OBELT: return "#ff4040";
    case OHAMMER: case OSWORD: case O2SWORD: case OHSWORD: case OSPEAR: case ODAGGER: case OBATTLEAXE:
    case OLONGSWORD: case OLANCE: case OVORPAL: case OSLAYER: return "#b0b0b8";
    case OWWAND: return "#40d040";
    case OPSTAFF: return "#d09050";
    case OCOOKIE: return "#d09050";
    case OBRASSLAMP: return "#ffff90";
    case OGOLDPILE: case OMAXGOLD: case OKGOLD: case ODGOLD: return "#ffe040";
    case ODIAMOND: case ORUBY: case OEMERALD: case OSAPPHIRE: case OLARNEYE: return "#ff60ff";
    }
    return "";
}

void wc_inv(WINDOW *w)
{
    char b[160];
    int i;
    ln = 0;
    line(w, A_BOLD, "Inventory");
    for (i = 0; i < MAXINVEN; i++) {
        if (!iven[i]) continue;
        item_name(b, sizeof b, i);
        line(w, c[WIELD] == i || c[WEAR] == i || c[SHIELD] == i ? A_BOLD : 0, b);
        be_invfg(ln - 1, wc_css(iven[i]));
    }
    for (i = ln; i < w->maxy; i++) be_invfg(i, "");
    while (ln < w->maxy) line(w, 0, "");
}
