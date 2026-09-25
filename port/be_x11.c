/* X11 frontend for the curses shim (from ~/Games/umoria/port): one window
 * per pane (Angband-style subwindows) plus an undecorated pop-up over the
 * map. Only the map is tiled (Amiga Larn 8x16 tiles from larn.org, scaled
 * nearest-neighbour, RVIP step 4); text panes use a normal font.
 * Env: LARN_SCALE (tile scale, default 2), LARN_TILES (tiles.rgba),
 * LARN_XFT (font family, default Menlo), LARN_TEXT (text px, 14),
 * LARN_MAP / _STATUS / _MSG / _INV = "x,y" window positions,
 * LARN_KEYLOG (print key codes). */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "curses.h"

#define TW 8  /* tile size in the sheet */
#define TH 16
#define PER_ROW 32

static Display *dpy;
static int scr;
static Visual *vis;
static Colormap cmap;
static GC gc;
static XftFont *mapfont, *txtfont, *boldfont;
static XftColor xcol[16];
static unsigned long pix[16];
static int scale = 2, cw, chh, tw, th, curP = -1, curY, curX;
static unsigned char *sheet; /* RGBA */
static int sheet_w, sheet_h;
static XImage *img;           /* one map cell, reused */
static unsigned char *istext; /* map cells drawn as text (cursor style) */

static struct pane {
    Window win;
    Pixmap pix;
    XftDraw *xd;
    int cols, rows, cw, ch, pad;
} P[NPANES];

static const char *pname[NPANES] = { "MAP", "STATUS", "MSG", "INV", "POP" };
static const char *ptitle[NPANES] = { "Larn", "Larn Status", "Larn Messages", "Larn Inventory", "" };

/* curses colours 0-7, then bold */
static const unsigned char pal[16][3] = {
    { 0, 0, 0 }, { 205, 49, 49 }, { 13, 188, 121 }, { 229, 229, 16 },
    { 76, 126, 255 }, { 188, 63, 188 }, { 17, 168, 205 }, { 215, 215, 215 },
    { 102, 102, 102 }, { 241, 76, 76 }, { 35, 209, 139 }, { 245, 245, 67 },
    { 110, 160, 255 }, { 214, 112, 214 }, { 41, 184, 219 }, { 255, 255, 255 },
};

static void load_sheet(void)
{
    const char *p = getenv("LARN_TILES");
    FILE *f = fopen(p ? p : "port/tiles.rgba", "rb");
    unsigned char h[8];
    if (!f || fread(h, 1, 8, f) != 8) { if (f) fclose(f); return; }
    sheet_w = h[0] | h[1] << 8 | h[2] << 16 | h[3] << 24;
    sheet_h = h[4] | h[5] << 8 | h[6] << 16 | h[7] << 24;
    sheet = malloc((size_t)sheet_w * sheet_h * 4);
    if (fread(sheet, 4, (size_t)sheet_w * sheet_h, f) != (size_t)sheet_w * sheet_h) {
        free(sheet);
        sheet = NULL;
    }
    fclose(f);
}

static XftFont *font(double px, double stretch, int weight)
{
    const char *fam = getenv("LARN_XFT");
    FcMatrix m;
    FcMatrixInit(&m);
    m.xx = stretch;
    return XftFontOpen(dpy, scr, XFT_FAMILY, XftTypeString, fam ? fam : "Menlo",
                       XFT_WEIGHT, XftTypeInteger, weight,
                       XFT_PIXEL_SIZE, XftTypeDouble, px, XFT_MATRIX, XftTypeMatrix, &m, NULL);
}

static void open_display(void)
{
    XGlyphInfo gi;
    const char *e;
    double px;
    int i;

    if (!(dpy = XOpenDisplay(NULL))) { fprintf(stderr, "larn: no X display\n"); exit(1); }
    scr = DefaultScreen(dpy);
    vis = DefaultVisual(dpy, scr);
    cmap = DefaultColormap(dpy, scr);
    if ((e = getenv("LARN_SCALE"))) scale = atoi(e) > 0 ? atoi(e) : 2;
    cw = TW * scale;
    chh = TH * scale;
    for (i = 0; i < 16; i++) {
        XRenderColor c = { pal[i][0] * 257, pal[i][1] * 257, pal[i][2] * 257, 0xffff };
        XColor xc;
        XftColorAllocValue(dpy, vis, cmap, &c, &xcol[i]);
        xc.red = c.red; xc.green = c.green; xc.blue = c.blue;
        XAllocColor(dpy, cmap, &xc);
        pix[i] = xc.pixel;
    }
    gc = XCreateGC(dpy, DefaultRootWindow(dpy), 0, NULL);
    /* map text: fills the 1:2 cell like the tiles */
    mapfont = font(chh * 0.8, 1, XFT_WEIGHT_BOLD);
    XftTextExtents8(dpy, mapfont, (FcChar8 *)"M", 1, &gi);
    mapfont = font(chh * 0.8, (cw * 0.95) / (gi.xOff > 0 ? gi.xOff : 1), XFT_WEIGHT_BOLD);
    px = (e = getenv("LARN_TEXT")) ? atof(e) : 14;
    txtfont = font(px, 1, XFT_WEIGHT_MEDIUM);
    boldfont = font(px, 1, XFT_WEIGHT_BOLD);
    XftTextExtents8(dpy, txtfont, (FcChar8 *)"M", 1, &gi);
    tw = gi.xOff;
    th = txtfont->ascent + txtfont->descent;
    img = XCreateImage(dpy, vis, DefaultDepth(dpy, scr), ZPixmap, 0, malloc(cw * chh * 4), cw, chh, 32, 0);
    load_sheet();
}

/* Default layout for a 1440x932 screen: map top left, Status and Inventory
 * stacked to its right, Messages below the map. XQuartz adds title bars. */
static void place(int p, int *x, int *y)
{
    char var[32];
    const char *e;
    int right = P[P_MAP].cols * cw + 6;
    *x = p == P_STATUS || p == P_INV ? right : 0;
    *y = p == P_MSG ? P[P_MAP].rows * chh + 28 : p == P_INV ? P[P_STATUS].rows * th + 28 : 0;
    snprintf(var, sizeof var, "LARN_%s", pname[p]);
    if ((e = getenv(var))) sscanf(e, "%d,%d", x, y);
}

static void make_pixmap(struct pane *q)
{
    int w = q->cols * q->cw + 2 * q->pad, h = q->rows * q->ch + 2 * q->pad;
    if (q->xd) XftDrawDestroy(q->xd);
    if (q->pix) XFreePixmap(dpy, q->pix);
    q->pix = XCreatePixmap(dpy, q->win, w, h, DefaultDepth(dpy, scr));
    q->xd = XftDrawCreate(dpy, q->pix, vis, cmap);
    XSetForeground(dpy, gc, pix[0]);
    XFillRectangle(dpy, q->pix, gc, 0, 0, w, h);
}

void be_init(int p, int cols, int rows)
{
    struct pane *q = &P[p];
    XSizeHints h;
    int x, y;

    if (!dpy) open_display();
    q->cols = cols;
    q->rows = rows;
    q->cw = p == P_MAP ? cw : tw;
    q->ch = p == P_MAP ? chh : th;
    if (p == P_MAP) istext = calloc(cols * rows, 1);
    place(p, &x, &y);
    q->win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), x, y, cols * q->cw, rows * q->ch, 0, pix[7], pix[0]);
    /* fixed size: no resize fight with XQuartz */
    h.flags = PPosition | USPosition | PMinSize | PMaxSize;
    h.x = x;
    h.y = y;
    h.min_width = h.max_width = cols * q->cw;
    h.min_height = h.max_height = rows * q->ch;
    XSetWMNormalHints(dpy, q->win, &h);
    XStoreName(dpy, q->win, ptitle[p]);
    XSelectInput(dpy, q->win, KeyPressMask | ExposureMask);
    make_pixmap(q);
    XMapWindow(dpy, q->win);
    XFlush(dpy);
}

/* The pop-up: no title bar, over the top left of the map, sized to its
 * content plus one character of padding. */
void be_popup(int rows, int cols)
{
    struct pane *q = &P[P_POP];
    int x, y, w, h;
    Window child;

    if (!rows) {
        if (q->win) XUnmapWindow(dpy, q->win);
        return;
    }
    q->cw = tw;
    q->ch = th;
    q->pad = tw;
    q->cols = cols;
    q->rows = rows;
    w = cols * tw + 2 * q->pad;
    h = rows * th + 2 * q->pad;
    XTranslateCoordinates(dpy, P[P_MAP].win, DefaultRootWindow(dpy), cw, 0, &x, &y, &child);
    if (!q->win) {
        XSetWindowAttributes a;
        a.override_redirect = True;
        a.background_pixel = pix[0];
        a.border_pixel = pix[7];
        q->win = XCreateWindow(dpy, DefaultRootWindow(dpy), x, y, w, h, 1, CopyFromParent, InputOutput,
                               CopyFromParent, CWOverrideRedirect | CWBackPixel | CWBorderPixel, &a);
        XSelectInput(dpy, q->win, KeyPressMask | ExposureMask);
    } else
        XMoveResizeWindow(dpy, q->win, x, y, w, h);
    make_pixmap(q);
    XMapRaised(dpy, q->win);
}

static void draw_text(struct pane *q, int y, int x, chtype ch)
{
    FcChar8 c = ch & A_CHARTEXT;
    int bold = !!(ch & A_BOLD);
    XftFont *f = q == &P[P_MAP] ? mapfont : bold ? boldfont : txtfont;
    int fgc = (ch & 0x800) ? PAIR_NUMBER(ch) : 7, inv = !!(ch & A_STANDOUT);
    int px = q->pad + x * q->cw, py = q->pad + y * q->ch;
    XGlyphInfo gi;
    if (fgc == 0) fgc = 7;
    if (bold) fgc += 8;
    XSetForeground(dpy, gc, inv ? pix[fgc] : pix[0]);
    XFillRectangle(dpy, q->pix, gc, px, py, q->cw, q->ch);
    if (ch & A_UNDERLINE) {
        XSetForeground(dpy, gc, pix[fgc]);
        XFillRectangle(dpy, q->pix, gc, px, py + q->ch - 1, q->cw, 1);
    }
    if (c == ' ') return;
    XftTextExtents8(dpy, f, &c, 1, &gi);
    XftDrawString8(q->xd, inv ? &xcol[0] : &xcol[fgc], f, px + (q->cw - gi.xOff) / 2,
                   py + (q->ch - f->ascent - f->descent) / 2 + f->ascent, &c, 1);
}

void be_put(int p, int y, int x, chtype ch, int tile)
{
    struct pane *q = &P[p];
    int px, py, tx, ty;
    if (!q->pix || y < 0 || x < 0 || y >= q->rows || x >= q->cols) return;
    if (p == P_MAP) istext[y * q->cols + x] = tile < 0;
    if (tile < 0 || !sheet || (tile / PER_ROW + 1) * TH > sheet_h) {
        draw_text(q, y, x, ch);
        return;
    }
    tx = (tile % PER_ROW) * TW;
    ty = (tile / PER_ROW) * TH;
    for (py = 0; py < chh; py++)
        for (px = 0; px < cw; px++) { /* nearest-neighbour, over black */
            unsigned char *s = sheet + ((ty + py / scale) * sheet_w + tx + px / scale) * 4;
            int a = s[3];
            XPutPixel(img, px, py, (unsigned long)(s[0] * a / 255) << 16 | (s[1] * a / 255) << 8 | (s[2] * a / 255));
        }
    XPutImage(dpy, q->pix, gc, img, 0, 0, x * cw, y * chh, cw, chh);
}

void be_cursor(int p, int y, int x)
{
    curP = p;
    curY = y;
    curX = x;
}

void be_sound(const char *event) { (void)event; }
void be_end(void) { }

void be_flush(void)
{
    int p;
    for (p = 0; p < NPANES; p++) {
        struct pane *q = &P[p];
        if (q->win && q->pix)
            XCopyArea(dpy, q->pix, q->win, gc, 0, 0, q->cols * q->cw + 2 * q->pad, q->rows * q->ch + 2 * q->pad, 0, 0);
    }
    /* cursor: a bar under text, an outline around tiles */
    if (curP >= 0 && P[curP].win && curY < P[curP].rows && curX < P[curP].cols) {
        struct pane *q = &P[curP];
        int px = q->pad + curX * q->cw, py = q->pad + curY * q->ch;
        XSetForeground(dpy, gc, pix[7]);
        if (curP != P_MAP || istext[curY * q->cols + curX])
            XFillRectangle(dpy, q->win, gc, px, py + q->ch - 2, q->cw, 2);
        else
            XDrawRectangle(dpy, q->win, gc, px, py, q->cw - 1, q->ch - 1);
    }
    XFlush(dpy);
}

/* Arrows and the keypad send digits: Larn turns them into hjklyubn at the
 * command prompt, the added menus use them as 8/2/4/6. */
static int keycode(XKeyEvent *ev)
{
    char buf[8];
    KeySym ks;
    int n = XLookupString(ev, buf, sizeof buf, &ks, NULL);
    switch (ks) {
    case XK_Left: case XK_KP_Left: case XK_KP_4: return '4';
    case XK_Right: case XK_KP_Right: case XK_KP_6: return '6';
    case XK_Up: case XK_KP_Up: case XK_KP_8: return '8';
    case XK_Down: case XK_KP_Down: case XK_KP_2: return '2';
    case XK_Home: case XK_KP_Home: case XK_KP_7: return '7';
    case XK_Prior: case XK_KP_Prior: case XK_KP_9: return '9';
    case XK_End: case XK_KP_End: case XK_KP_1: return '1';
    case XK_Next: case XK_KP_Next: case XK_KP_3: return '3';
    case XK_KP_Begin: case XK_KP_5: return '5';
    case XK_KP_Enter: case XK_Return: return '\r';
    case XK_BackSpace: case XK_Delete: return '\b';
    case XK_KP_Add: return '+';
    case XK_KP_Subtract: return '-';
    case XK_KP_Multiply: return '*';
    case XK_KP_Divide: return '/';
    case XK_KP_Decimal: case XK_KP_Delete: return '.';
    case XK_KP_0: case XK_KP_Insert: return '0';
    }
    return n == 1 ? (unsigned char)buf[0] : -1;
}

int be_getkey(int wait)
{
    XEvent ev;
    for (;;) {
        if (!wait && !XPending(dpy)) return -1;
        XNextEvent(dpy, &ev);
        if (ev.type == Expose)
            be_flush();
        else if (ev.type == KeyPress) {
            int k = keycode(&ev.xkey);
            if (getenv("LARN_KEYLOG")) fprintf(stderr, "key %d\n", k);
            if (k >= 0) return k;
        }
    }
}
void be_invfg(int y, const char *css) { }

void be_prompt(const char *s) { }   /* web only: the prompt line over the map */
