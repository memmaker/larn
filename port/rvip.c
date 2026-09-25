/* RVIP additions for Larn, called from parse() (main.c) and whatitem():
 *   x        auto-explore: one step per turn over what the player knows
 *   < >      off the stairs: walk to the nearest known one, take it there
 *   Enter    floating menu of every command (from larn.help)
 *   i        inventory with a cursor and item menus
 *   item prompts ("quaff which?") show the list with a cursor */
#include <stdio.h>
#include <string.h>
#include "../larncons.h"
#include "../larndata.h"
#include "../larnfunc.h"
#include "../io.h"
#include "../action.h"
#include "../display.h"
#include "curses.h"

#define MAXINVEN 26 /* inventory.c */
#define ESC 27
#define CTRL(c) ((c) & 31)
#define LIST_ROWS 18 /* list rows in a pop-up (the map area is 17 + prompt) */

void item_name(char *b, size_t n, int i); /* tiles.c */
void wc_push(const char *keys);            /* wcurses.c */
extern int wc_msgs;                        /* wcurses.c: messages so far */

/* ---------------- auto-explore and stair walking ---------------- */

static char auto_mode;     /* 'x' explore, '<' / '>' walk to stairs */
static int auto_level = -1, auto_msgs, auto_x = -1, auto_y;
static unsigned char visited[MAXLEVEL + MAXVLEVEL][MAXX][MAXY];

/* moveplayer() directions 1-8 (display.c) and their keys in parse() */
#define dx diroffx
#define dy diroffy
static const char dkey[9] = { 0, 'j', 'l', 'k', 'h', 'u', 'y', 'n', 'b' };

/* things you walk onto without an effect */
static int plain(int o)
{
    switch (o) {
    case 0: case OOPENDOOR: case OSHOREWATER: case OCOOLEDLAVA:
    case OIVTELETRAP: case OIVDARTRAP: case OIVTRAPDOOR: case OTRAPARROWIV:
        return 1;
    }
    return 0;
}

/* things lying around that explore visits (pick-ups, not fixtures) */
static int loot(int o)
{
    if (o == OSTATUE || o == OANNIHILATION || o == OWATER || o == OLAVA || o == OINNERWALL) return 0;
    return o == OCHEST || o == OBOOK || o == OSCROLL || o == OPOTION || o == OCOOKIE || o == OGOLDPILE ||
           o == OMAXGOLD || o == OKGOLD || o == ODGOLD || (o >= ODIAMOND && o <= OSAPPHIRE) ||
           o == OLARNEYE || o == OORB || o == OAMULET || o == OORBOFDRAGON || o == OSPIRITSCARAB ||
           o == OCUBEofUNDEAD || o == ONOTHEFT || o == OURN || o == OSPHTAILSMAN ||
           (o >= OPLATE && o <= OBELT && o != OWALL) || (o >= OBATTLEAXE && o <= OSSPLATE) || o == OSHIELD ||
           o == OELVENCHAIN || o == OVORPAL || o == OSLAYER || o == OHSWORD || o == OSPEAR || o == ODAGGER;
}

/* where < / > walk to. Never the town's volcanic shaft (deep, deadly):
 * it works as before when you stand on it. */
static int stairs_for(char mode, int o)
{
    if (mode == '<') return o == OSTAIRSUP || o == OVOLUP || (level == 1 && o == OENTRANCE);
    return o == OSTAIRSDOWN || (level == 0 && o == OENTRANCE);
}

static int passable(char mode, int x, int y)
{
    int o = item[x][y];
    if (!know[x][y]) return 0;
    if (plain(o) || loot(o) || o == OCLOSEDDOOR) return 1;
    return mode != 'x' && stairs_for(mode, o);
}

static int target(char mode, int x, int y)
{
    int i, j;
    if (mode != 'x') return know[x][y] && stairs_for(mode, item[x][y]);
    if (visited[level][x][y] || !passable(mode, x, y)) return 0;
    if (loot(item[x][y])) return 1;
    for (i = -1; i <= 1; i++)
        for (j = -1; j <= 1; j++)
            if (x + i >= 0 && y + j >= 0 && x + i < MAXX && y + j < MAXY && !know[x + i][y + j]) return 1;
    return 0;
}

/* a monster the player can see: the screen shows its letter */
static int monster_in_view(void)
{
    int x, y;
    for (y = 0; y < MAXY; y++)
        for (x = 0; x < MAXX; x++)
            if (mitem[x][y] && (int)(stdscr->c[y * stdscr->maxx + x] & A_CHARTEXT) == monstnamelist[mitem[x][y]])
                return 1;
    return 0;
}

/* direction (1-8) of the first step to the nearest target, 0 = none */
static int first_step(char mode)
{
    static short from[MAXX][MAXY], queue[MAXX * MAXY];
    static unsigned char done[MAXX][MAXY];
    int head = 0, tail = 0, d;
    memset(done, 0, sizeof done);
    queue[tail++] = (short)(playerx * MAXY + playery);
    done[playerx][playery] = 1;
    while (head < tail) {
        int x = queue[head] / MAXY, y = queue[head] % MAXY;
        head++;
        if ((x != playerx || y != playery) && target(mode, x, y)) {
            while (from[x][y] != playerx * MAXY + playery) {
                int p = from[x][y];
                x = p / MAXY;
                y = p % MAXY;
            }
            for (d = 1; d <= 8; d++)
                if (playerx + dx[d] == x && playery + dy[d] == y) return d;
            return 0;
        }
        for (d = 1; d <= 8; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= MAXX || ny >= MAXY || done[nx][ny]) continue;
            if (!passable(mode, nx, ny) && !target(mode, nx, ny)) continue;
            /* a closed door is a place to open, not to walk through diagonally */
            done[nx][ny] = 1;
            from[nx][ny] = (short)(x * MAXY + y);
            queue[tail++] = (short)(nx * MAXY + ny);
        }
    }
    return 0;
}

static int stop(const char *why)
{
    auto_mode = 0;
    if (why) {
        cursors();
        lprcat(why);
    }
    return 0;
}

/* One step of auto_mode: a command key for parse(), -1 when the turn was
 * used here (door opened), 0 when stopped. */
static int auto_step(void)
{
    char mode = auto_mode;
    int d, x, y;

    visited[level][playerx][playery] = 1;
    if (level != auto_level) return stop(NULL); /* new level */
    /* arrived ("You have found ..." is expected here) */
    if (mode != 'x' && stairs_for(mode, item[playerx][playery])) {
        auto_mode = 0;
        return level == 0 && item[playerx][playery] == OENTRANCE ? 'E' : mode;
    }
    if (wc_msgs != auto_msgs) return stop(NULL); /* something happened */
    if (hitflag) return stop(NULL);
    if (c[BLINDCOUNT] || c[CONFUSE]) return stop("\nYou are in no state to explore.");
    if (monster_in_view()) return stop("\nNot with a monster in view.");
    if (playerx == auto_x && playery == auto_y) return stop(NULL); /* the last step didn't move */
    d = first_step(mode);
    if (!d)
        return stop(mode == 'x' ? "\nNothing left to explore." : mode == '<' ? "\nYou know of no way up." :
                                                                                "\nYou know of no way down.");
    x = playerx + dx[d];
    y = playery + dy[d];
    if (item[x][y] == OCLOSEDDOOR) {
        cursors();
        if (!act_open_door(x, y)) return stop(NULL); /* stuck: the message says so */
        show1cell(x, y);
        auto_msgs = wc_msgs; /* "The door opens" doesn't stop us */
        auto_x = -1;
        return -1;
    }
    auto_msgs = wc_msgs;
    auto_x = playerx;
    auto_y = playery;
    return dkey[d];
}

static int start(char mode)
{
    auto_mode = mode;
    auto_level = level;
    auto_msgs = wc_msgs;
    auto_x = -1;
    return auto_step();
}

/* ---------------- Enter: command menu ---------------- */

struct entry {
    int key;
    char text[40];
};
static struct entry cmds[80];
static int ncmds;

/* larn.help page 1: three columns (0, 27, 55) of "k  text"; lines that
 * start with blanks continue the entry above in that column. */
static void load_cmds(void)
{
    static const int col[4] = { 0, 27, 55, 80 };
    FILE *f = fopen("larn.help", "r");
    char line[256];
    int last[3] = { -1, -1, -1 }, n = 0, c3;
    struct entry cols[3][30];
    int cn[3] = { 0, 0, 0 }, cont_skip[3] = { 0, 0, 0 };

    if (!f) return;
    while (fgets(line, sizeof line, f) && n < 24 + 23) {
        n++;
        if (n < 26 || n > 46) continue; /* intro page and page header */
        line[strcspn(line, "\r\n")] = 0;
        for (c3 = 0; c3 < 3; c3++) {
            char part[64], *p;
            int len = (int)strlen(line), a = col[c3], b = col[c3 + 1], k;
            if (a >= len) continue;
            if (b > len) b = len;
            memcpy(part, line + a, b - a);
            part[b - a] = 0;
            for (k = (int)strlen(part); k > 0 && part[k - 1] == ' ';) part[--k] = 0;
            if (!*part) continue;
            if (*part == ' ') { /* continuation */
                struct entry *e;
                if (last[c3] < 0 || cont_skip[c3]) continue;
                e = &cols[c3][last[c3]];
                for (p = part; *p == ' '; p++) ;
                if (strlen(e->text) + strlen(p) + 2 < sizeof e->text) {
                    strcat(e->text, " ");
                    strcat(e->text, p);
                }
                continue;
            }
            last[c3] = -1;
            p = strchr(part, ' ');
            if (!p) continue;
            *p++ = 0;
            while (*p == ' ') p++;
            if (*p == '>') { /* "< > off the stairs ...": a note, and its lines */
                last[c3] = -1;
                cont_skip[c3] = 1;
                continue;
            }
            cont_skip[c3] = 0;
            if (strlen(part) == 1) k = part[0];
            else if (part[0] == '^' && strlen(part) == 2) k = CTRL(part[1]);
            else continue; /* "Enter", "< >": notes, not commands */
            if (k == 'x' && c3 == 2) continue;
            last[c3] = cn[c3];
            cols[c3][cn[c3]].key = k;
            snprintf(cols[c3][cn[c3]].text, sizeof cols[c3][0].text, "%s", p);
            cn[c3]++;
        }
    }
    fclose(f);
    ncmds = 0;
    for (c3 = 0; c3 < 3; c3++)
        for (n = 0; n < cn[c3]; n++) cmds[ncmds++] = cols[c3][n];
    cmds[ncmds].key = 'x';
    strcpy(cmds[ncmds++].text, "auto-explore the level");
}

static void key_name(char *b, int k)
{
    if (k < ' ') sprintf(b, "^%c", k + '@');
    else sprintf(b, "%c", k);
}

/* Draws rows[] as a list over the map (top left), row `cur` highlighted,
 * scrolled so it shows. Returns the first row shown. */
static int draw_list(const char *title, char rows[][80], int n, int cur, int top)
{
    int i, w = title ? (int)strlen(title) : 0, shown = n < LIST_ROWS ? n : LIST_ROWS, y = 0;
    for (i = 0; i < n; i++)
        if ((int)strlen(rows[i]) > w) w = (int)strlen(rows[i]);
    if (cur < top) top = cur;
    if (cur >= top + shown) top = cur - shown + 1;
    for (i = 0; i < LIST_ROWS + 1; i++) {
        move(i, 0);
        clrtoeol();
    }
    if (title) {
        move(y++, 0);
        attrset(A_BOLD);
        addstr(title);
        attrset(A_NORMAL);
    }
    for (i = top; i < top + shown; i++) {
        int k;
        move(y++, 0);
        attrset(i == cur ? A_STANDOUT : A_NORMAL);
        addstr(rows[i]);
        for (k = (int)strlen(rows[i]); k < w; k++) addch(' ');
        attrset(A_NORMAL);
    }
    move(title ? 0 : cur - top, 0);
    return top;
}

static void close_list(void) { draws(0, MAXX, 0, MAXY); }

static int getkey(void)
{
    nodelay(stdscr, FALSE);
    return getch();
}

int cmd_menu(void)
{
    static char rows[80][80];
    int i, cur = 0, top = 0, k, res = 0;
    if (!ncmds) load_cmds();
    if (!ncmds) return 0;
    for (i = 0; i < ncmds; i++) {
        char kn[4];
        key_name(kn, cmds[i].key);
        snprintf(rows[i], sizeof rows[i], " %-3s %s", kn, cmds[i].text);
    }
    wc_overlay();
    for (;;) {
        top = draw_list(NULL, rows, ncmds, cur, top);
        k = getkey();
        if (k == ESC || k == '0') break;
        if (k == '8') cur = (cur + ncmds - 1) % ncmds;
        else if (k == '2') cur = (cur + 1) % ncmds;
        else if (k == '9') cur = cur > LIST_ROWS ? cur - LIST_ROWS : 0;
        else if (k == '3') cur = cur + LIST_ROWS < ncmds ? cur + LIST_ROWS : ncmds - 1;
        else if (k == '\r' || k == '\n' || k == '5' || k == ' ') {
            res = cmds[cur].key;
            break;
        } else {
            for (i = 0; i < ncmds; i++)
                if (cmds[i].key == k) res = k;
            if (res) break;
        }
    }
    close_list();
    return res;
}

/* ---------------- inventory: cursor lists and item menus ---------------- */

static int is_armor(int o)
{
    switch (o) {
    case OLEATHER: case OCHAIN: case OPLATE: case ORING: case OSPLINT:
    case OPLATEARMOR: case OSTUDLEATHER: case OSSPLATE: case OSHIELD: case OELVENCHAIN:
        return 1;
    }
    return 0;
}

static int is_weapon(int o)
{
    switch (o) {
    case OSWORDofSLASHING: case OHAMMER: case OSWORD: case O2SWORD: case OHSWORD: case OSPEAR:
    case ODAGGER: case OBATTLEAXE: case OLONGSWORD: case OLANCE: case OVORPAL: case OSLAYER:
        return 1;
    }
    return 0;
}

struct action {
    int key;
    const char *name;
};

/* actions for inventory slot i, main one first */
static int actions(int i, struct action *a)
{
    int o = iven[i], n = 0;
    int worn = c[WEAR] == i || c[SHIELD] == i;
    if (o == OPOTION) a[n++] = (struct action){ 'q', "Quaff" };
    else if (o == OSCROLL || o == OBOOK) a[n++] = (struct action){ 'r', "Read" };
    else if (o == OCOOKIE) a[n++] = (struct action){ 'e', "Eat" };
    else if (is_weapon(o) && c[WIELD] != i) a[n++] = (struct action){ 'w', "Wield" };
    else if (is_weapon(o)) a[n++] = (struct action){ 'w', "Put away (wield nothing)" };
    else if (is_armor(o) && !worn) a[n++] = (struct action){ 'W', "Wear" };
    else if (is_armor(o)) a[n++] = (struct action){ 'T', "Take off" };
    a[n++] = (struct action){ 'd', "Drop" };
    a[n++] = (struct action){ '*', "Examine" };
    return n;
}

/* the keys the player would type; 'n' first when the floor item would be
 * offered instead (floor_consume() asks y/n) */
static void run_action(int key, int i)
{
    char k[4] = { 0 };
    int n = 0, f = item[playerx][playery];
    if (key == '*') {
        char b[128];
        item_name(b, sizeof b, i);
        cursors();
        lprintf("\n%s", b + 3);
        return;
    }
    k[n++] = (char)key;
    if ((key == 'q' && f == OPOTION) || (key == 'r' && (f == OSCROLL || f == OBOOK)) || (key == 'e' && f == OCOOKIE))
        k[n++] = 'n';
    if (key == 'w' && c[WIELD] == i) k[n++] = '-';
    else if (key != 'T') k[n++] = (char)('a' + i);
    wc_push(k);
}

static int item_rows(char rows[][80], int *slot, int (*want)(int))
{
    int i, n = 0;
    for (i = 0; i < MAXINVEN; i++)
        if (iven[i] && (!want || want(iven[i]))) {
            item_name(rows[n], 80, i);
            slot[n++] = i;
        }
    return n;
}

/* Floating menu of the actions for slot i; the chosen key or 0. */
static int item_menu(int i)
{
    struct action a[6];
    char rows[6][80], title[80];
    int n = actions(i, a), cur = 0, k, j;
    item_name(title, sizeof title, i);
    for (j = 0; j < n; j++) snprintf(rows[j], sizeof rows[j], " %c  %s", a[j].key, a[j].name);
    for (;;) {
        draw_list(title + 3, rows, n, cur, 0);
        k = getkey();
        if (k == '8') cur = (cur + n - 1) % n;
        else if (k == '2') cur = (cur + 1) % n;
        else if (k == '5' || k == '6' || k == '\r' || k == '\n' || k == ' ') return a[cur].key;
        else if (k == ESC || k == '4' || k == '0' || k == '.') return 0;
        else
            for (j = 0; j < n; j++)
                if (a[j].key == k) return k;
    }
}

static int reopen; /* 2: an item action is queued, 1: reopen the inventory next tick */

/* 'i': the inventory with a cursor. Returns a key for parse() or 0. */
int inventory_browse(void)
{
    static char rows[MAXINVEN][80];
    static int cur;
    int slot[MAXINVEN], n, k, i, top = 0, key = 0;
    struct action a[6];
    reopen = 0;
    wc_overlay();
    for (;;) {
        n = item_rows(rows, slot, NULL);
        if (!n) {
            close_list();
            cursors();
            lprcat("\nYou aren't carrying anything.");
            return 0;
        }
        if (cur >= n) cur = n - 1;
        top = draw_list("Inventory: letter uses, Shift drops, Ctrl examines, Enter menu", rows, n, cur, top);
        k = getkey();
        if (k == ESC || k == '0' || k == '.' || k == 'i') break;
        if (k == '8') { cur = (cur + n - 1) % n; continue; }
        if (k == '2') { cur = (cur + 1) % n; continue; }
        if (k == '4' || k == '6') continue; /* one list: Larn has no separate equipment */
        i = slot[cur];
        if (k == '\r' || k == '\n' || k == '5' || k == ' ') key = item_menu(i);
        else if (k == '+') key = (actions(i, a), a[0].key);
        else if (k == '-') key = 'd';
        else if (k == '*') key = '*';
        else if ((k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z') || (k >= 1 && k <= 26)) {
            int letter = k >= 'a' ? k - 'a' : k >= 'A' ? k - 'A' : k - 1;
            for (i = 0; i < n && slot[i] != letter; i++) ;
            if (i == n) break; /* not an item: a normal command */
            cur = i;
            i = slot[cur];
            key = k >= 'a' ? (actions(i, a), a[0].key) : k >= 'A' ? 'd' : '*';
        } else
            break;
        if (!key) continue; /* menu closed: back to the list */
        close_list();
        run_action(key, i);
        if (key != '*' && !monster_in_view()) reopen = 2; /* after the queued command */
        return 0;
    }
    close_list();
    if (k != ESC && k != '0' && k != '.' && k != 'i') {
        char s[2] = { (char)k, 0 };
        wc_push(s); /* any other key is a normal command */
    }
    return 0;
}

/* Item prompts: the list of what fits the verb, with a cursor. Returns what
 * whatitem() returns: a letter, '-', '.', '*' or ESC. */
static int want_potion(int o) { return o == OPOTION; }
static int want_read(int o) { return o == OSCROLL || o == OBOOK; }
static int want_eat(int o) { return o == OCOOKIE; }
static int want_wear(int o) { return is_armor(o); }

int rvip_whatitem(const char *verb)
{
    static char rows[MAXINVEN][80];
    char title[96];
    int slot[MAXINVEN], n, k, cur = 0, top = 0, key = ESC;
    int (*want)(int) = NULL;
    /* the verbs are whatitem()'s callers' own constant strings */
    if (!strcmp(verb, "quaff")) want = want_potion;
    else if (!strcmp(verb, "read")) want = want_read;
    else if (!strcmp(verb, "eat")) want = want_eat;
    else if (!strcmp(verb, "wear")) want = want_wear;
    else if (!strncmp(verb, "wield", 5)) want = is_weapon;
    n = item_rows(rows, slot, want);
    if (!n) n = item_rows(rows, slot, NULL); /* nothing fits: show everything */
    snprintf(title, sizeof title, "What do you want to %s? (Enter chooses, Esc)", verb);
    wc_overlay();
    for (;;) {
        top = draw_list(title, rows, n, cur, top);
        k = getkey();
        if (k == '8' && n) cur = (cur + n - 1) % n;
        else if (k == '2' && n) cur = (cur + 1) % n;
        else if (k == '4' || k == '6') ;
        else if ((k == '\r' || k == '\n' || k == '5') && n) { key = 'a' + slot[cur]; break; }
        else if (k == '0') break;
        else if (k == ESC || k == '-' || k == '.' || k == '*' || (k >= 'a' && k <= 'z')) { key = k; break; }
    }
    close_list();
    cursors();
    return key;
}

/* ---------------- parse() hook ---------------- */

/* Called with yylex()'s key (0 = none). Returns the key for parse() to run,
 * 0 = nothing this tick, -1 = the turn was used here. */
int rvip_command(int k)
{
    if (k == 0) {
        if (auto_mode) return auto_step();
        if (reopen == 1) {
            reopen = 0;
            if (!monster_in_view()) return inventory_browse();
        }
        return 0;
    }
    reopen = reopen == 2; /* the queued command itself keeps it */
    auto_mode = 0;
    if (k == 13 || k == '\n') k = cmd_menu();
    if (k == 'x') return start('x');
    if (k == 'i') return inventory_browse();
    if (k == '<' || k == '>') {
        int o = item[playerx][playery];
        if (k == '<' ? o == OSTAIRSUP || o == OVOLUP : o == OSTAIRSDOWN || o == OVOLDOWN)
            return k; /* on them: as before */
        return start((char)k);
    }
    return k;
}
