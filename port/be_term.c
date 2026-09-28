/* Terminal frontend for the curses shim (native release builds): draws the
 * panes into one text screen with ANSI/VT escapes, no curses library.
 * Linux and macOS: termios; Windows: console VT mode and _getch().
 * Layout: row 0 the live message, the map below it, then Status; the
 * Inventory pane to the right of the map when the terminal is wide enough;
 * the pop-up boxed over the top left of the map. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#endif
#include "curses.h"

#define SW 200                  /* screen buffer size */
#define SH 80

static struct pane { int cols, rows, ec, er; chtype *c; } P[NPANES];
static char prompt[512];
static chtype scr[SH][SW], out[SH][SW];
static int curP = -1, curY, curX, started, term_w = 80, term_h = 24, full = 1;

#ifdef _WIN32
static HANDLE hin, hout;
static DWORD in_mode, out_mode;
#else
static struct termios saved;
#endif

static void term_size(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO i;
    if (GetConsoleScreenBufferInfo(hout, &i)) {
        term_w = i.srWindow.Right - i.srWindow.Left + 1;
        term_h = i.srWindow.Bottom - i.srWindow.Top + 1;
    }
#else
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col) { term_w = ws.ws_col; term_h = ws.ws_row; }
#endif
    if (term_w > SW) term_w = SW;
    if (term_h > SH) term_h = SH;
}

static void term_start(void)
{
    if (started) return;
    started = 1;
#ifdef _WIN32
    hin = GetStdHandle(STD_INPUT_HANDLE);
    hout = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(hin, &in_mode);
    GetConsoleMode(hout, &out_mode);
    SetConsoleMode(hout, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
    SetConsoleMode(hin, in_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT));
    SetConsoleOutputCP(437);
#else
    struct termios t;
    tcgetattr(0, &saved);
    t = saved;
    t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    t.c_iflag &= ~(IXON | ICRNL | INLCR);
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
#endif
    term_size();
    fputs("\033[?1049h\033[H\033[2J", stdout);  /* alternate screen */
    fflush(stdout);
}

static void term_stop(void)
{
    if (!started) return;
    started = 0;
    fputs("\033[0m\033[?25h\033[?1049l", stdout);
    fflush(stdout);
#ifdef _WIN32
    SetConsoleMode(hin, in_mode);
    SetConsoleMode(hout, out_mode);
#else
    tcsetattr(0, TCSANOW, &saved);
#endif
}

void be_init(int p, int cols, int rows)
{
    int i;
    term_start();
    P[p].cols = cols; P[p].rows = rows;
    P[p].ec = cols; P[p].er = rows;
    free(P[p].c);
    P[p].c = malloc(sizeof(chtype) * cols * rows);
    for (i = 0; i < cols * rows; i++) P[p].c[i] = ' ';
}

void be_popup(int rows, int cols)
{
    if (!rows) { P[P_POP].rows = 0; full = 1; return; }
    be_init(P_POP, cols, rows);
}

void be_put(int p, int y, int x, chtype ch, int tile)
{
    struct pane *q = &P[p];
    (void)tile;
    if (!q->c || y < 0 || x < 0 || y >= q->rows || x >= q->cols) return;
    q->c[y * q->cols + x] = ch;
}

void be_cursor(int p, int y, int x) { curP = p; curY = y; curX = x; }
void be_rows(int p, int rows) { P[p].er = rows; full = 1; }
void be_line(int p, int y, const char *s, const char *css, int tile)
{
    chtype so = 0;
    int x;
    (void)css; (void)tile;
    for (x = 0; x < P[p].cols; x++) {
        while (*s == 1 || *s == 2) so = *s++ == 1 ? A_STANDOUT : 0;
        be_put(p, y, x, (*s ? (unsigned char)*s++ : ' ') | so, -1);
    }
}
void be_prompt(const char *s) { snprintf(prompt, sizeof prompt, "%s", s); }

static void blit(int p, int y0, int x0, int rows, int cols)
{
    struct pane *q = &P[p];
    int y, x;
    for (y = 0; y < rows && y < q->rows && y0 + y < term_h; y++)
        for (x = 0; x < cols && x < q->cols && x0 + x < term_w; x++)
            scr[y0 + y][x0 + x] = q->c[y * q->cols + x];
}

/* where each pane starts on the screen */
static int map_y = 1, stat_y, inv_x;

void be_flush(void)
{
    int y, x, cy = 0, cx = 0, pop_y = 2, pop_x = 2;
    static int last_w, last_h;
    char buf[64];

    if (!started) return;
    term_size();
    if (term_w != last_w || term_h != last_h) { last_w = term_w; last_h = term_h; full = 1; }
    for (y = 0; y < term_h; y++) for (x = 0; x < term_w; x++) scr[y][x] = ' ';

    for (x = 0; prompt[x] && x < term_w; x++) scr[0][x] = (unsigned char)prompt[x];
    blit(P_MAP, map_y, 0, P[P_MAP].rows, P[P_MAP].cols);
    stat_y = map_y + P[P_MAP].rows;
    blit(P_STATUS, stat_y, 0, P[P_STATUS].rows, P[P_STATUS].cols);
    inv_x = P[P_MAP].cols + 2;
    if (term_w - inv_x >= 20 && P[P_INV].c) blit(P_INV, map_y, inv_x, P[P_INV].er, term_w - inv_x);

    if (P[P_POP].rows) {
        struct pane *q = &P[P_POP];
        int h = q->rows + 2, w = q->cols + 4;
        if (pop_y + h > term_h) pop_y = term_h - h > 0 ? term_h - h : 0;
        if (pop_x + w > term_w) pop_x = term_w - w > 0 ? term_w - w : 0;
        for (y = 0; y < h && pop_y + y < term_h; y++)
            for (x = 0; x < w && pop_x + x < term_w; x++) {
                chtype c = ' ';
                if (y == 0 || y == h - 1) c = (x == 0 || x == w - 1) ? '+' : '-';
                else if (x == 0 || x == w - 1) c = '|';
                scr[pop_y + y][pop_x + x] = c;
            }
        blit(P_POP, pop_y + 1, pop_x + 2, q->rows, q->cols);
    }

    switch (curP) {
    case P_MAP: cy = map_y + curY; cx = curX; break;
    case P_STATUS: cy = stat_y + curY; cx = curX; break;
    case P_MSG: cy = 0; cx = curX; break;
    case P_INV: cy = map_y + curY; cx = inv_x + curX; break;
    case P_POP: cy = pop_y + 1 + curY; cx = pop_x + 2 + curX; break;
    default: cy = 0; cx = (int)strlen(prompt); break;
    }

    if (full) { fputs("\033[0m\033[2J", stdout); memset(out, 0xff, sizeof out); full = 0; }
    fputs("\033[?25l", stdout);
    for (y = 0; y < term_h; y++) {
        int at = -1, rev = -1;
        for (x = 0; x < term_w; x++) {
            chtype c = scr[y][x];
            int ch = c & A_CHARTEXT, r = !!(c & A_STANDOUT);
            if (out[y][x] == c) continue;
            out[y][x] = c;
            if (at != x) { snprintf(buf, sizeof buf, "\033[%d;%dH", y + 1, x + 1); fputs(buf, stdout); }
            if (r != rev) { fputs(r ? "\033[7m" : "\033[0m", stdout); rev = r; }
            putchar(ch < ' ' || ch == 127 ? ' ' : ch);
            at = x + 1;
        }
        if (rev == 1) fputs("\033[0m", stdout);
    }
    if (cy >= term_h) cy = term_h - 1;
    if (cx >= term_w) cx = term_w - 1;
    snprintf(buf, sizeof buf, "\033[%d;%dH\033[?25h", cy + 1, cx + 1);
    fputs(buf, stdout);
    fflush(stdout);
}

/* raw input */
#ifdef _WIN32
static int key_ready(int ms)
{
    DWORD t = GetTickCount();
    do {
        if (_kbhit()) return 1;
        if (ms) Sleep(5);
    } while ((int)(GetTickCount() - t) < ms);
    return 0;
}
static int readc(void) { return _getch(); }
#else
static int key_ready(int ms)
{
    fd_set s;
    struct timeval tv;
    FD_ZERO(&s); FD_SET(0, &s);
    tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
    return select(1, &s, NULL, NULL, &tv) > 0;
}
static int readc(void)
{
    unsigned char c;
    if (read(0, &c, 1) != 1) { term_stop(); exit(0); }
    return c;
}
#endif

static int readkey(void)
{
    int c = readc();
#ifdef _WIN32
    if (c == 0 || c == 224) {
        switch (readc()) {
        case 72: return KEY_UP; case 80: return KEY_DOWN;
        case 75: return KEY_LEFT; case 77: return KEY_RIGHT;
        case 71: return KEY_A1; case 73: return KEY_A3;
        case 79: return KEY_C1; case 81: return KEY_C3;
        case 76: return KEY_B2; case 83: return '\b';
        }
        return -1;
    }
    if (c == 3) return 3;
#else
    if (c == 27 && key_ready(30)) {     /* escape sequence */
        int a = readc(), b, n = 0;
        if (a != '[' && a != 'O') return a == 27 ? 27 : a;
        b = readc();
        while (b >= '0' && b <= '9') { n = n * 10 + b - '0'; b = readc(); }
        switch (b) {
        case 'A': return KEY_UP; case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT; case 'D': return KEY_LEFT;
        case 'H': return KEY_A1; case 'F': return KEY_C1;
        case 'E': return KEY_B2;
        case '~':
            switch (n) {
            case 1: case 7: return KEY_A1; case 4: case 8: return KEY_C1;
            case 5: return KEY_A3; case 6: return KEY_C3; case 3: return '\b';
            }
        }
        return -1;
    }
    if (c == 127) return '\b';
#endif
    if (c == '\n') return '\r';
    return c;
}

int be_getkey(int wait)
{
    for (;;) {
        int k;
        if (wait <= 0 && !key_ready(0)) {
            /* polling (auto-explore): paint each step, a short pause;
             * wait -1 only drains the queue */
            if (!wait) { be_flush(); key_ready(30); }
            return -1;
        }
        be_flush();
        if ((k = readkey()) >= 0) return k;
    }
}

void be_end(void) { term_stop(); }
void be_sound(const char *s) { (void)s; }
int be_icons(void) { return 0; }

