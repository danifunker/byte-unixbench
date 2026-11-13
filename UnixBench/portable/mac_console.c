/*******************************************************************************
 *  UnixBench portable harness - console window for classic Mac OS
 *
 *  A text window that stands in for stdout and stdin.  Retro68's C library
 *  sends file descriptors 0-2 to _consoleread() and _consolewrite(); this
 *  file defines them, so Retro68's own RetroConsole library is not linked.
 *  RetroConsole is C++ and brings in libstdc++, which made the application
 *  550 KB on 68k and 1 MB on PowerPC, and ld emits two R_68K_32 relocations
 *  for one .eh_frame word of that library, which the Elf2Mac in Retro68 as
 *  of November 2025 turns into a truncated relocation list (the start-up
 *  code then patches the wrong words; newer Elf2Mac refuses to link
 *  instead).  Without C++ the application is under 100 KB and its
 *  relocation lists are clean.
 *
 *  Plain C89 and the original Toolbox only: Monaco 9 in a fixed grid of
 *  cells in a document window, no scrollback, and a line editor for input
 *  (Backspace, Return; Command-Q or the close box quits).  Events are
 *  only handled while a line is being read, so the window is not redrawn
 *  while a benchmark pass runs.
 ******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <MacTypes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Events.h>
#include <Menus.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <OSUtils.h>
#include <SegLoad.h>

#define CON_MAX_ROWS 60
#define CON_MAX_COLS 100
#define CON_MARGIN   4
#define CON_LINE_MAX 256

ssize_t _consolewrite(int fd, const void *buf, size_t count);
ssize_t _consoleread(int fd, void *buf, size_t count);

static WindowPtr con_win;
static int con_inited;
static int con_rows, con_cols;
static int con_cx, con_cy;              /* cursor cell */
static short con_cellw, con_cellh, con_ascent;
static char con_cells[CON_MAX_ROWS][CON_MAX_COLS];
static unsigned char con_dirty[CON_MAX_ROWS];
static int con_all_dirty;
static int con_cursor_on;

static char con_line[CON_LINE_MAX];     /* last line read, handed out in pieces */
static int con_line_len, con_line_pos;

static const unsigned char con_title[] = "\011UnixBench";

static void con_cell_rect(int col, int row, Rect *r)
{
    SetRect(r, (short)(CON_MARGIN + col * con_cellw),
            (short)(CON_MARGIN + row * con_cellh),
            (short)(CON_MARGIN + (col + 1) * con_cellw),
            (short)(CON_MARGIN + (row + 1) * con_cellh));
}

static void con_draw_row(int row)
{
    Rect r;

    SetRect(&r, CON_MARGIN, (short)(CON_MARGIN + row * con_cellh),
            (short)(CON_MARGIN + con_cols * con_cellw),
            (short)(CON_MARGIN + (row + 1) * con_cellh));
    EraseRect(&r);
    MoveTo(CON_MARGIN, (short)(CON_MARGIN + row * con_cellh + con_ascent));
    DrawText((Ptr)con_cells[row], 0, (short)con_cols);
}

static void con_toggle_cursor(void)
{
    Rect r;

    con_cell_rect(con_cx < con_cols ? con_cx : con_cols - 1, con_cy, &r);
    InvertRect(&r);
}

/* Redraw the rows that changed (all of them after a scroll or an update
   event).  The port is the console window while drawing. */
static void con_flush(void)
{
    GrafPtr saved;
    int i, cursor = con_cursor_on;

    GetPort(&saved);
    SetPort((GrafPtr)con_win);
    if (cursor)
        con_toggle_cursor();
    for (i = 0; i < con_rows; ++i) {
        if (con_all_dirty || con_dirty[i]) {
            con_draw_row(i);
            con_dirty[i] = 0;
        }
    }
    con_all_dirty = 0;
    if (cursor)
        con_toggle_cursor();
    SetPort(saved);
}

static void con_show_cursor(int on)
{
    GrafPtr saved;

    if (on == con_cursor_on)
        return;
    GetPort(&saved);
    SetPort((GrafPtr)con_win);
    con_toggle_cursor();
    SetPort(saved);
    con_cursor_on = on;
}

static void con_newline(void)
{
    con_cx = 0;
    if (con_cy < con_rows - 1) {
        ++con_cy;
        return;
    }
    memmove(con_cells[0], con_cells[1], (size_t)(con_rows - 1) * CON_MAX_COLS);
    memset(con_cells[con_rows - 1], ' ', CON_MAX_COLS);
    con_all_dirty = 1;
}

static void con_putc(int c)
{
    switch (c) {
    case '\n':
        con_newline();
        return;
    case '\r':
        con_cx = 0;
        return;
    case '\b':
        if (con_cx > 0)
            --con_cx;
        return;
    case '\t':
        do {
            con_putc(' ');
        } while (con_cx % 8 != 0 && con_cx < con_cols);
        return;
    case '\a':
    case 27:
        return;
    default:
        break;
    }
    if (c < 32)
        c = '?';
    if (con_cx >= con_cols)
        con_newline();
    con_cells[con_cy][con_cx++] = (char)c;
    con_dirty[con_cy] = 1;
}

static void con_init(void)
{
    Rect r;
    FontInfo fi;
    EventRecord ev;
    int i;

    if (con_inited)
        return;
    con_inited = 1;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    /* Under MultiFinder the application is only brought to the front once
       it has asked for events (Technote TB 35). */
    for (i = 0; i < 5; ++i)
        EventAvail(everyEvent, &ev);

    /* As much of the screen as fits a whole number of cells, below the
       menu bar and the title bar. */
    r = qd.screenBits.bounds;
    r.top += 40;
    r.left += 4;
    r.right -= 4;
    r.bottom -= 4;
    con_win = NewWindow(NULL, &r, con_title, 1, noGrowDocProc,
                        (WindowPtr)-1L, 1, 0);
    if (con_win == NULL)
        ExitToShell();
    SetPort((GrafPtr)con_win);
    TextFont(kFontIDMonaco);
    TextSize(9);
    GetFontInfo(&fi);
    con_cellw = CharWidth('W');
    con_cellh = (short)(fi.ascent + fi.descent + fi.leading);
    con_ascent = fi.ascent;
    if (con_cellw < 1)
        con_cellw = 6;
    if (con_cellh < 1)
        con_cellh = 11;
    con_cols = (r.right - r.left - 2 * CON_MARGIN) / con_cellw;
    con_rows = (r.bottom - r.top - 2 * CON_MARGIN) / con_cellh;
    if (con_cols > CON_MAX_COLS)
        con_cols = CON_MAX_COLS;
    if (con_rows > CON_MAX_ROWS)
        con_rows = CON_MAX_ROWS;
    if (con_cols < 1)
        con_cols = 1;
    if (con_rows < 1)
        con_rows = 1;
    SizeWindow(con_win, (short)(con_cols * con_cellw + 2 * CON_MARGIN),
               (short)(con_rows * con_cellh + 2 * CON_MARGIN), 1);
    memset(con_cells, ' ', sizeof(con_cells));
    con_all_dirty = 1;
    InitCursor();
}

static void con_read_line(void)
{
    EventRecord ev;
    WindowPtr w;
    GrafPtr saved;
    int c;

    con_line_len = 0;
    con_line_pos = 0;
    con_flush();
    con_show_cursor(1);
    for (;;) {
        SystemTask();
        if (!GetNextEvent(everyEvent, &ev))
            continue;
        switch (ev.what) {
        case keyDown:
        case autoKey:
            c = (int)(ev.message & charCodeMask);
            if (ev.modifiers & cmdKey) {
                if (c == 'q' || c == 'Q' || c == '.')
                    ExitToShell();
                break;
            }
            if (c == '\r' || c == 3) {          /* Return or Enter */
                con_show_cursor(0);
                con_line[con_line_len++] = '\n';
                con_putc('\n');
                con_flush();
                return;
            }
            if (c == '\b' || c == 127) {
                if (con_line_len > 0) {
                    con_show_cursor(0);
                    --con_line_len;
                    con_putc('\b');
                    con_cells[con_cy][con_cx] = ' ';
                    con_dirty[con_cy] = 1;
                    con_flush();
                    con_show_cursor(1);
                }
            } else if (c >= 32 && con_line_len < CON_LINE_MAX - 2) {
                con_show_cursor(0);
                con_line[con_line_len++] = (char)c;
                con_putc(c);
                con_flush();
                con_show_cursor(1);
            }
            break;
        case mouseDown:
            switch (FindWindow(ev.where, &w)) {
            case inSysWindow:
                SystemClick(&ev, w);
                break;
            case inDrag:
                DragWindow(w, ev.where, &qd.screenBits.bounds);
                break;
            case inGoAway:
                if (TrackGoAway(w, ev.where))
                    ExitToShell();
                break;
            case inContent:
                if (w != FrontWindow())
                    SelectWindow(w);
                break;
            default:
                break;
            }
            break;
        case updateEvt:
            w = (WindowPtr)ev.message;
            GetPort(&saved);
            SetPort((GrafPtr)w);
            BeginUpdate(w);
            if (w == con_win) {
                con_all_dirty = 1;
                con_flush();
            }
            EndUpdate(w);
            SetPort(saved);
            break;
        default:
            break;
        }
    }
}

ssize_t _consolewrite(int fd, const void *buf, size_t count)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t i;

    (void)fd;
    con_init();
    for (i = 0; i < count; ++i)
        con_putc(p[i]);
    con_flush();
    return (ssize_t)count;
}

ssize_t _consoleread(int fd, void *buf, size_t count)
{
    size_t n;

    (void)fd;
    con_init();
    if (con_line_pos >= con_line_len)
        con_read_line();
    n = (size_t)(con_line_len - con_line_pos);
    if (n > count)
        n = count;
    memcpy(buf, con_line + con_line_pos, n);
    con_line_pos += (int)n;
    return (ssize_t)n;
}
