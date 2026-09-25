/* Minimal curses for Larn (RVIP port): one in-memory 80x24 screen, drawn by
 * an X11 (or web) frontend as Angband-style panes. Found before the system
 * curses via -Iport; only what Larn uses. Adapted from ~/Games/umoria/port. */
#ifndef LARN_WCURSES_H
#define LARN_WCURSES_H
#include <stdio.h>

typedef unsigned int chtype;
#define ERR (-1)
#define OK 0
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* char in bits 0-7, colour (0-7) in 8-10, colour set flag 11, attributes */
#define A_CHARTEXT 0xffu
#define A_COLOR 0xf00u
#define COLOR_PAIR(n) ((chtype)(((n) & 7) | 8) << 8)
#define PAIR_NUMBER(a) ((int)(((a) >> 8) & 7))
#define A_NORMAL 0u
#define A_STANDOUT 0x1000u
#define A_REVERSE A_STANDOUT
#define A_BOLD 0x2000u
#define A_UNDERLINE 0x4000u
#define A_DIM 0x8000u
#define A_BLINK 0u
#define A_ATTRIBUTES 0xff00u

#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7

/* keys: the frontend already sends letters for arrows and the keypad */
#define KEY_DOWN 0402
#define KEY_UP 0403
#define KEY_LEFT 0404
#define KEY_RIGHT 0405
#define KEY_A1 0534
#define KEY_A3 0535
#define KEY_B2 0536
#define KEY_C1 0537
#define KEY_C3 0540
#define KEY_ENTER 0527

typedef struct {
    int maxy, maxx, cury, curx;
    chtype attr;
    short *first, *last; /* changed range per line, -1 = none */
    chtype *c;
} WINDOW;

extern WINDOW *stdscr, *curscr;
extern int LINES, COLS;

WINDOW *initscr(void);
int endwin(void);
WINDOW *newwin(int, int, int, int);
int wmove(WINDOW *, int, int);
int waddch(WINDOW *, chtype);
int waddstr(WINDOW *, const char *);
int wclear(WINDOW *);
int wclrtoeol(WINDOW *);
int wclrtobot(WINDOW *);
int wrefresh(WINDOW *);
int wgetch(WINDOW *);
int nodelay(WINDOW *, int);
int curs_set(int);
int has_colors(void);
int wc_kbhit(void);       /* a key is waiting (explore stops on it) */

#define move(y, x) wmove(stdscr, y, x)
#define addch(ch) waddch(stdscr, ch)
#define addstr(s) waddstr(stdscr, s)
#define clear() wclear(stdscr)
#define clrtoeol() wclrtoeol(stdscr)
#define clrtobot() wclrtobot(stdscr)
#define refresh() wrefresh(stdscr)
#define getch() wgetch(stdscr)
#define attrset(a) (stdscr->attr = (chtype)(a), OK)
#define attron(a) (stdscr->attr |= (chtype)(a), OK)
#define attroff(a) (stdscr->attr &= ~(chtype)(a), OK)
#define standout() attron(A_STANDOUT)
#define standend() attrset(A_NORMAL)
#define keypad(w, b) OK
#define cbreak() OK
#define nocbreak() OK
#define noecho() OK
#define echo() OK
#define nonl() OK
#define nl() OK
#define intrflush(w, b) OK
#define start_color() OK
#define use_default_colors() OK
#define init_pair(n, f, b) OK
#define flushinp() OK

/* Hooks the game calls (io.c, display.c, inventory.c) */
void wc_dungeon(void);      /* the map was drawn: back from text screens */
void wc_overlay(void);      /* text is about to be drawn over the map */
void wc_msgnew(void);       /* a new message line starts (the scroll ring) */

/* Frontend: panes. Only the map is tiled; text goes to text panes and a
 * pop-up box sized to its content. */
enum { P_MAP, P_STATUS, P_MSG, P_INV, P_POP, NPANES };
void be_init(int pane, int cols, int rows);
void be_put(int pane, int y, int x, chtype ch, int tile);
void be_cursor(int pane, int y, int x); /* pane -1: no cursor */
void be_popup(int rows, int cols);      /* 0: close */
void be_flush(void);
int be_getkey(int wait);                /* -1 when !wait and nothing queued */
void be_sound(const char *event);       /* web: play a sound event */
void be_end(void);                      /* web: the game is over (or saved) */
int tile_for(int y, int x, chtype ch);  /* tiles.c: -1 = draw as text */
void wc_status(WINDOW *);               /* tiles.c: Status pane */
const char *wc_css(int obj);             /* tiles.c: an object's colour */
void be_invfg(int y, const char *css);   /* inventory row colour */
void wc_inv(WINDOW *);                  /* tiles.c: Inventory pane */
#endif
