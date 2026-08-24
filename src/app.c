/* nosleep -- keeps Windows awake while a chosen app is actually working.

   This file owns the window, the once-a-second tick, and the wiring between
   the process sampler, the activity rule and the sleep lock. */

#include "wincompat.h"
#include "config.h"
#include "theme.h"
#include "ui.h"
#include "tray.h"
#include "icon.h"
#include "applist.h"
#include "monitor.h"
#include "activity.h"
#include "power.h"
#include "netstat.h"
#include "logfile.h"
#include "settings.h"

/* ------------------------------------------------------------- constants */

#define ID_LIST        100

#define TIMER_TICK     1
#define MSG_TRAY       (WM_APP + 1)
#define MSG_SHOW_ME    (WM_APP + 2)

#define MENU_SHOW      201
#define MENU_RELEASE   202
#define MENU_EXIT      203

/* Layout in logical pixels; everything is scaled through ui_scale. The
   height of the list, and therefore of the window, is decided at startup
   from the monitor work area -- a fixed height overflows a 768 px screen. */
#define WIN_W     430
#define PAD        16
#define HEAD_Y     14
#define HEAD_H     38   /* a heading line and a line of explanation */
#define LIST_Y     58
#define INFO_H     16
#define ROW_H      40
#define ROWS_MAX    8
#define ROWS_MIN    3

/* The same heading treatment over the sliders, so neither half of the window
   is a set of controls with nothing to say what they are for. */
#define PANEL_HEAD_H 34

/* One sensitivity row: label, slider and threshold on top, the live reading
   underneath. */
#define CH_ROW_H    38
#define CH_LABEL_W  42
#define CH_SLIDER_W 150

#define CHK_H      22
#define BTN_H      46

#define WIN_STYLE (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX)

/* Everything below the list: gap, info, gap, four rows, gap, checkbox, gap,
   button, bottom padding. */
/* The four channels, then the wait. The wait is not a channel -- it is how
   long everything has to stay under bar before the lock goes -- but it earns
   the same kind of row because measurement says it is the control that
   actually decides whether work is told apart from idleness. */
#define SL_WAIT   CH_COUNT
#define SL_COUNT  (CH_COUNT + 1)

#define BELOW_LIST (8 + INFO_H + 14 + PANEL_HEAD_H + (CH_ROW_H * SL_COUNT) \
                    + 10 + CHK_H + 10 + BTN_H + 14)

typedef enum { MODE_PICK = 0, MODE_WATCH = 1 } Mode;

typedef enum { HOT_NONE = 0, HOT_BUTTON, HOT_CHECK, HOT_REFRESH } HotItem;

/* ----------------------------------------------------------------- state */

static HINSTANCE g_inst;
static HWND      g_wnd;
static HWND      g_list;
static Theme     g_theme;
static UiList    g_list_state;

static AppEntry  g_apps[CFG_MAX_APPS];
static int       g_app_count;

static Mode      g_mode = MODE_PICK;

/* The sampler runs as soon as an app is picked, not only once the button is
   pressed: the numbers under the sliders are the whole reason to have them,
   and they are worth seeing before committing to anything. In pick mode it
   only feeds the display -- the sleep lock is untouched. */
static int       g_sampling;
static Monitor   g_monitor;
static Activity  g_activity;
static ActivityConfig g_cfg;
static ActivityState g_state = ACT_IDLE;

static UiSlider  g_slider[SL_COUNT];
static int       g_dragging = -1;   /* which slider, or -1 */

static int       g_keep_display;
static WCHAR     g_watch_title[128];
static WCHAR     g_watch_exe[64];
static unsigned int g_watch_pid;

static HICON     g_icon_big;
static HICON     g_icon_small;

static HotItem   g_hot;
static HotItem   g_pressed;
static WCHAR     g_notice[128];

/* ----------------------------------------------------------- tiny string */

static void wcopy(WCHAR *dst, const WCHAR *src, int cap)
{
    int i = 0;
    if (cap <= 0) return;
    while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int wlen(const WCHAR *s)
{
    int i = 0;
    while (s[i]) i++;
    return i;
}

/* ---------------------------------------------------------------- layout */

/* Computed once, in device pixels. */
static int g_list_h;
static int g_info_y;
static int g_head2_y;
static int g_panel_y;
static int g_check_y;
static int g_button_y;
static int g_client_h;

/* `y` is already in device pixels; x, w and h are logical. */
static RECT rect_at(int x, int y, int w, int h)
{
    RECT r;
    r.left = ui_scale(x);
    r.top = y;
    r.right = ui_scale(x + w);
    r.bottom = y + ui_scale(h);
    return r;
}

static RECT rect_of(int x, int y, int w, int h)
{
    return rect_at(x, ui_scale(y), w, h);
}

static void compute_layout(void)
{
    RECT work, frame;
    int non_client, room, rows;

    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.left = 0;
        work.top = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    /* How much the caption and borders add on top of the client area. */
    frame.left = 0; frame.top = 0; frame.right = 100; frame.bottom = 100;
    AdjustWindowRect(&frame, WIN_STYLE, FALSE);
    non_client = (frame.bottom - frame.top) - 100;

    room = (work.bottom - work.top) - non_client - ui_scale(24)
           - ui_scale(LIST_Y) - ui_scale(BELOW_LIST);

    rows = room / ui_scale(ROW_H);
    if (rows > ROWS_MAX) rows = ROWS_MAX;
    if (rows < ROWS_MIN) rows = ROWS_MIN;

    g_list_h   = rows * ui_scale(ROW_H);
    g_info_y   = ui_scale(LIST_Y) + g_list_h + ui_scale(8);
    g_head2_y  = g_info_y + ui_scale(INFO_H + 14);
    g_panel_y  = g_head2_y + ui_scale(PANEL_HEAD_H);
    g_check_y  = g_panel_y + ui_scale(CH_ROW_H * SL_COUNT + 10);
    g_button_y = g_check_y + ui_scale(CHK_H + 10);
    g_client_h = g_button_y + ui_scale(BTN_H + 14);
}

static int channel_row_y(int i)
{
    return g_panel_y + ui_scale(CH_ROW_H * i);
}

static RECT button_rect(void) { return rect_at(PAD, g_button_y, WIN_W - 2 * PAD, BTN_H); }
static RECT check_rect(void)  { return rect_at(PAD, g_check_y, 240, CHK_H); }

static RECT refresh_rect(void)
{
    return rect_at(WIN_W - PAD - 70, g_info_y - ui_scale(2), 70, INFO_H + 4);
}

static int in_rect(const RECT *r, int x, int y)
{
    return x >= r->left && x < r->right && y >= r->top && y < r->bottom;
}

/* Keeps every slider's track rectangle in step with the layout. */
static void place_sliders(void)
{
    int i;
    for (i = 0; i < SL_COUNT; i++)
        g_slider[i].track = rect_at(PAD + CH_LABEL_W, channel_row_y(i),
                                    CH_SLIDER_W, 20);
}

/* -------------------------------------------------------------- app list */

static void sample_app(unsigned int pid);
static int  have_readings(void);

static void refresh_apps(void)
{
    unsigned int keep = 0;
    int i;

    if (g_list_state.sel >= 0 && g_list_state.sel < g_app_count)
        keep = g_apps[g_list_state.sel].pid;

    g_app_count = applist_collect(g_apps, CFG_MAX_APPS);
    ui_list_set_count(g_list, g_app_count);

    /* Keep the user's choice pointed at the same app even though the list is
       in z-order and may well have reshuffled. */
    g_list_state.sel = -1;
    if (keep) {
        for (i = 0; i < g_app_count; i++) {
            if (g_apps[i].pid == keep) { g_list_state.sel = i; break; }
        }
    }

    /* Readings follow the selection, so they are there to look at before
       anything is committed to. */
    if (g_mode == MODE_PICK)
        sample_app(g_list_state.sel >= 0 ? g_apps[g_list_state.sel].pid : 0);

    InvalidateRect(g_list, 0, FALSE);
    InvalidateRect(g_wnd, 0, FALSE);
}

/* ------------------------------------------------------------ row drawing */

static void draw_row(HDC dc, int index, const RECT *r, int selected, int hot,
                     void *user)
{
    const UiFonts *f = ui_fonts();
    const AppEntry *a = &g_apps[index];
    WCHAR sub[96];
    RECT title, subtitle;
    COLORREF tc = selected ? g_theme.text_sel : g_theme.text;

    (void)hot; (void)user;

    if (!g_list_state.enabled) tc = g_theme.text_dim;

    title = *r;
    title.left += ui_scale(12);
    title.right -= ui_scale(10);
    title.top += ui_scale(5);
    title.bottom = title.top + ui_scale(17);

    subtitle = title;
    subtitle.top = title.bottom;
    subtitle.bottom = subtitle.top + ui_scale(15);

    ui_text(dc, &title, a->title[0] ? a->title : a->exe, f->body, tc,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    wcopy(sub, a->exe[0] ? a->exe : L"?", 96);
    {
        int n = wlen(sub);
        WCHAR tail[32];
        wsprintfW(tail, L"  \x00b7  PID %u", a->pid);
        wcopy(sub + n, tail, 96 - n);
    }
    ui_text(dc, &subtitle, sub, f->small, g_theme.text_dim,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}

/* -------------------------------------------------- formatting the numbers */

static void fmt_bytes_rate(WCHAR *out, unsigned int bps)
{
    if (bps >= 1024u * 1024u)
        wsprintfW(out, L"%u.%u MB/s", bps >> 20, ((bps >> 16) & 15) * 10 / 16);
    else if (bps >= 1024u)
        wsprintfW(out, L"%u.%u KB/s", bps >> 10, ((bps & 1023) * 10) >> 10);
    else
        wsprintfW(out, L"%u B/s", bps);
}

static void fmt_count_rate(WCHAR *out, unsigned int per_sec)
{
    if (per_sec >= 1000000u)
        wsprintfW(out, L"%u.%uM/s", per_sec / 1000000u,
                  (per_sec % 1000000u) / 100000u);
    else if (per_sec >= 1000u)
        wsprintfW(out, L"%u.%uK/s", per_sec / 1000u, (per_sec % 1000u) / 100u);
    else
        wsprintfW(out, L"%u/s", per_sec);
}

static void fmt_channel(WCHAR *out, int ch, unsigned int v)
{
    switch (ch) {
    case CH_CPU:  wsprintfW(out, L"%u.%u%%", v / 10, v % 10); break;
    case CH_DISK:
    case CH_NET:  fmt_bytes_rate(out, v); break;
    default:      fmt_count_rate(out, v); break;
    }
}

static const WCHAR *channel_label(int ch)
{
    switch (ch) {
    case CH_CPU:  return L"CPU";
    case CH_DISK: return L"Disk";
    case CH_NET:  return L"Net";
    case CH_MEM:  return L"RAM";
    default:      return L"Wait";
    }
}

/* Milliseconds as something readable: 45s, 2m, 2m 30s. */
static void fmt_wait(WCHAR *out, unsigned int ms)
{
    unsigned int s = ms / 1000;
    if (s < 60)          wsprintfW(out, L"%us", s);
    else if (s % 60 == 0) wsprintfW(out, L"%um", s / 60);
    else                 wsprintfW(out, L"%um %us", s / 60, s % 60);
}

/* CPU as a percentage of one core, so 340 means three and a bit cores. */
static unsigned int cpu_percent(void)
{
    return g_activity.ch[CH_CPU].smoothed / 10;
}

static void fmt_status(WCHAR *out, int cap)
{
    WCHAR line[192];
    WCHAR value[40];

    if (g_state == ACT_BUSY) {
        if (g_activity.why < 0) {
            wcopy(line, L"holding \x00b7 measuring...", 192);
        } else {
            fmt_channel(value, g_activity.why,
                        g_activity.ch[g_activity.why].smoothed);
            wsprintfW(line, L"working \x00b7 %s %s",
                      channel_label(g_activity.why), value);
        }
    } else if (g_state == ACT_GRACE) {
        unsigned int quiet = (unsigned int)(g_activity.quiet_ms / 1000);
        unsigned int total = g_cfg.wait.threshold / 1000;
        wsprintfW(line, L"quiet %us of %us \x00b7 still holding", quiet, total);
    } else {
        wsprintfW(line, L"released \x00b7 waiting for work");
    }

    if (!power_ok())
        wcopy(line + wlen(line), L"  (Windows refused the lock)", 40);

    wcopy(out, line, cap);
}

static TrayState tray_state_now(void)
{
    if (g_state == ACT_BUSY) return TRAY_HOLDING;
    if (g_state == ACT_GRACE) return TRAY_GRACE;
    return TRAY_OFF;
}

static void update_tray(void)
{
    WCHAR tip[128];
    WCHAR name[64];

    wcopy(name, g_watch_exe[0] ? g_watch_exe : g_watch_title, 64);

    if (g_state == ACT_IDLE)
        wsprintfW(tip, L"nosleep \x00b7 %s \x00b7 released", name);
    else if (g_state == ACT_GRACE)
        wsprintfW(tip, L"nosleep \x00b7 %s \x00b7 quiet, still holding", name);
    else
        wsprintfW(tip, L"nosleep \x00b7 %s \x00b7 %u%% of one core", name,
                  cpu_percent());

    tray_set(tray_state_now(), tip);
}

/* -------------------------------------------------------------- painting */

static void paint_checkbox(HDC dc)
{
    const UiFonts *f = ui_fonts();
    RECT r = check_rect();
    RECT box, label;
    COLORREF frame = (g_hot == HOT_CHECK) ? g_theme.accent : g_theme.border;

    box.left = r.left;
    box.top = r.top + (r.bottom - r.top - ui_scale(16)) / 2;
    box.right = box.left + ui_scale(16);
    box.bottom = box.top + ui_scale(16);

    if (g_keep_display) {
        ui_fill_round(dc, &box, ui_scale(3), g_theme.accent);
        /* A tick drawn with two strokes: cheaper than a font glyph and it
           lands on the pixel grid at any scale. */
        {
            HPEN p = CreatePen(PS_SOLID, ui_scale(2), g_theme.accent_text);
            HGDIOBJ op = SelectObject(dc, p);
            int x = box.left, y = box.top, s = ui_scale(16);
            MoveToEx(dc, x + s * 3 / 10, y + s / 2, 0);
            LineTo(dc, x + s * 9 / 20, y + s * 7 / 10);
            LineTo(dc, x + s * 3 / 4, y + s * 3 / 10);
            SelectObject(dc, op);
            DeleteObject(p);
        }
    } else {
        ui_frame_round(dc, &box, ui_scale(3), frame);
    }

    label = r;
    label.left = box.right + ui_scale(9);
    ui_text(dc, &label, L"Keep the screen on too", f->body, g_theme.text,
            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

static void paint_button(HDC dc)
{
    const UiFonts *f = ui_fonts();
    RECT r = button_rect();
    const WCHAR *label;
    COLORREF back, fore;
    int usable;

    if (g_mode == MODE_WATCH) {
        label = L"RELEASE";
        usable = 1;
    } else {
        label = L"DON'T SLEEP";
        usable = (g_list_state.sel >= 0);
    }

    if (!usable) {
        back = g_theme.muted;
        fore = g_theme.muted_text;
    } else if (g_pressed == HOT_BUTTON) {
        back = g_theme.accent_down;
        fore = g_theme.accent_text;
    } else if (g_hot == HOT_BUTTON) {
        back = g_theme.accent_hot;
        fore = g_theme.accent_text;
    } else {
        back = g_theme.accent;
        fore = g_theme.accent_text;
    }

    ui_fill_round(dc, &r, ui_scale(6), back);
    ui_text(dc, &r, label, f->button, fore,
            DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
}

/* The four sensitivity rows. Each shows where its threshold sits and, right
   beneath it, what the watched app is doing at this instant -- so the line
   the app has to cross is never abstract. */
static void paint_channels(HDC dc)
{
    const UiFonts *f = ui_fonts();
    int i;

    for (i = 0; i < CH_COUNT; i++) {
        int y = channel_row_y(i);
        RECT label, value, live;
        WCHAR text[80], now[40];
        const Channel *c = &g_activity.ch[i];
        int active = have_readings();
        COLORREF lc;

        label = rect_at(PAD, y, CH_LABEL_W, 20);
        ui_text(dc, &label, channel_label(i), f->body, g_theme.text_dim,
                DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        ui_draw_slider(dc, &g_theme, &g_slider[i], 1);

        /* The threshold, in the channel's own units. */
        fmt_channel(text, i, g_cfg.ch[i].threshold);
        value = rect_at(PAD + CH_LABEL_W + CH_SLIDER_W + 8, y,
                        WIN_W - 2 * PAD - CH_LABEL_W - CH_SLIDER_W - 8, 20);
        ui_text(dc, &value, text, f->body, g_theme.text,
                DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);

        /* And what it is doing right now. */
        live = rect_at(PAD + CH_LABEL_W, y + ui_scale(18),
                       WIN_W - 2 * PAD - CH_LABEL_W, 16);
        if (!active) {
            wcopy(text, g_sampling ? L"measuring\x2026"
                                   : L"pick an app to see its numbers", 80);
            lc = g_theme.text_dim;
        } else {
            fmt_channel(now, i, c->smoothed);
            if (i == CH_MEM && g_activity.ws_bytes) {
                unsigned int mb = (unsigned int)(g_activity.ws_bytes >> 20);
                if (mb >= 1024)
                    wsprintfW(text, L"now %s  \x00b7  %u.%u GB in use", now,
                              mb >> 10, ((mb & 1023) * 10) >> 10);
                else
                    wsprintfW(text, L"now %s  \x00b7  %u MB in use", now, mb);
            } else if (i == CH_NET) {
                wsprintfW(text, L"now %s  \x00b7  %d connection%s", now,
                          g_activity.conns,
                          g_activity.conns == 1 ? L"" : L"s");
            } else {
                wsprintfW(text, L"now %s", now);
            }
            lc = c->busy ? g_theme.ok : g_theme.text_dim;
        }
        ui_text(dc, &live, text, f->small, lc,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        /* A bar the app's own noise pushed up is worth saying out loud. */
        if (active && c->learned_used) {
            RECT mark = rect_at(WIN_W - PAD - 64, y + ui_scale(18), 64, 16);
            ui_text(dc, &mark, L"auto-raised", f->small, g_theme.warn,
                    DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        }
    }

    /* The wait, which measurement says is the control that really decides
       whether work is told apart from an app just ticking over. */
    {
        int y = channel_row_y(SL_WAIT);
        RECT label = rect_at(PAD, y, CH_LABEL_W, 20);
        RECT value = rect_at(PAD + CH_LABEL_W + CH_SLIDER_W + 8, y,
                             WIN_W - 2 * PAD - CH_LABEL_W - CH_SLIDER_W - 8, 20);
        RECT live  = rect_at(PAD + CH_LABEL_W, y + ui_scale(18),
                             WIN_W - 2 * PAD - CH_LABEL_W, 16);
        WCHAR text[80], span[24];

        ui_text(dc, &label, L"Wait", f->body, g_theme.text_dim,
                DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        ui_draw_slider(dc, &g_theme, &g_slider[SL_WAIT], 1);

        fmt_wait(span, g_cfg.wait.threshold);
        ui_text(dc, &value, span, f->body, g_theme.text,
                DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);

        if (g_mode == MODE_WATCH && g_state != ACT_BUSY) {
            WCHAR sofar[24];
            fmt_wait(sofar, (unsigned int)g_activity.quiet_ms);
            wsprintfW(text, L"quiet for %s of %s", sofar, span);
        } else if (have_readings()) {
            /* Before the button is pressed the verdict is still worth
               showing: it is what would happen if it were. */
            wsprintfW(text, g_activity.why >= 0
                          ? L"working right now \x00b7 this would hold the lock"
                          : L"quiet right now \x00b7 nothing is over its limit");
        } else {
            wsprintfW(text, L"how long it must stay quiet before letting go");
        }
        ui_text(dc, &live, text, f->small,
                (g_mode == MODE_WATCH && g_state == ACT_GRACE) ? g_theme.warn
                                                               : g_theme.text_dim,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

static void paint_step(HDC dc, int y, const WCHAR *title, const WCHAR *sub);

static void paint_header(HDC dc)
{
    const UiFonts *f = ui_fonts();
    RECT r = rect_of(PAD, HEAD_Y, WIN_W - 2 * PAD, HEAD_H);

    if (g_mode == MODE_WATCH) {
        RECT dot, name, status;
        WCHAR line[224];
        COLORREF c = (g_state == ACT_BUSY) ? g_theme.ok
                   : (g_state == ACT_GRACE) ? g_theme.warn
                   : g_theme.off;

        dot.left = r.left;
        dot.top = r.top + ui_scale(5);
        dot.right = dot.left + ui_scale(9);
        dot.bottom = dot.top + ui_scale(9);
        ui_fill_round(dc, &dot, ui_scale(4), c);

        name = r;
        name.left += ui_scale(17);
        name.bottom = name.top + ui_scale(18);
        ui_text(dc, &name, g_watch_title[0] ? g_watch_title : g_watch_exe,
                f->head, g_theme.text,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        fmt_status(line, 224);
        status = r;
        status.left += ui_scale(17);
        status.top = name.bottom;
        ui_text(dc, &status, line, f->small, g_theme.text_dim,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        paint_step(dc, ui_scale(HEAD_Y),
                   L"1  \x2022  Which app are you waiting on?",
                   L"Windows will stay awake while that app is working.");
    }
}

/* Both halves of the window get a numbered heading and a line saying what
   they are for. Without them the window is a list and four unlabelled
   sliders, and nothing tells you what to do with either. */
static void paint_step(HDC dc, int y, const WCHAR *title, const WCHAR *sub)
{
    const UiFonts *f = ui_fonts();
    RECT t = rect_at(PAD, y, WIN_W - 2 * PAD, 19);
    RECT s = rect_at(PAD, y + ui_scale(19), WIN_W - 2 * PAD, 15);

    ui_text(dc, &t, title, f->head, g_theme.text,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    ui_text(dc, &s, sub, f->small, g_theme.text_dim,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void paint_info(HDC dc)
{
    const UiFonts *f = ui_fonts();
    RECT left = rect_at(PAD, g_info_y, 260, INFO_H);
    RECT right = refresh_rect();
    WCHAR line[128];

    if (g_notice[0]) {
        wcopy(line, g_notice, 128);
    } else if (g_mode == MODE_WATCH) {
        wsprintfW(line, L"watching PID %u and everything it spawns",
                  g_watch_pid);
    } else {
        wsprintfW(line, L"%d open apps", g_app_count);
    }

    ui_text(dc, &left, line, f->small, g_theme.text_dim,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    if (g_mode == MODE_PICK)
        ui_text(dc, &right, L"Refresh", f->small,
                (g_hot == HOT_REFRESH) ? g_theme.accent : g_theme.text_dim,
                DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
}

static void paint_window(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc, mem;
    HBITMAP bmp, old_bmp;
    RECT rc, frame;

    dc = BeginPaint(h, &ps);
    GetClientRect(h, &rc);

    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    old_bmp = (HBITMAP)SelectObject(mem, bmp);

    ui_fill(mem, &rc, g_theme.bg);

    /* The list is a child window; this is the hairline around it. */
    frame = rect_at(PAD - 1, ui_scale(LIST_Y) - 1, WIN_W - 2 * PAD + 2, 0);
    frame.bottom = ui_scale(LIST_Y) + g_list_h + 1;
    ui_frame_round(mem, &frame, ui_scale(6), g_theme.border);

    paint_header(mem);
    paint_info(mem);
    paint_step(mem, g_head2_y, L"2  \x2022  What counts as working?",
               L"Each row is a limit. Cross any one and it stays awake.");
    paint_channels(mem);
    paint_checkbox(mem);
    paint_button(mem);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

/* ------------------------------------------------------------- sampling */

/* Points the sampler at a process, or stops it when given nothing. Cheap to
   call repeatedly: pointing it at what it is already watching does nothing,
   which matters because the list refreshes whenever the window is activated. */
static void sample_app(unsigned int pid)
{
    if (pid == 0) {
        if (g_sampling) KillTimer(g_wnd, TIMER_TICK);
        g_sampling = 0;
        return;
    }

    if (g_sampling && g_monitor.alive && g_monitor.root_pid == pid) return;

    if (!monitor_start(&g_monitor, pid)) {
        if (g_sampling) KillTimer(g_wnd, TIMER_TICK);
        g_sampling = 0;
        return;
    }

    activity_init(&g_activity, &g_cfg);
    g_sampling = 1;
    SetTimer(g_wnd, TIMER_TICK, CFG_POLL_MS, 0);
}

/* True once a real rate has been measured. The first tick only establishes a
   baseline, so there is a second or two with nothing worth showing. */
static int have_readings(void)
{
    return g_sampling && g_activity.ch[CH_CPU].rlen > 0;
}

/* --------------------------------------------------------- mode switching */

static void log_thresholds(void)
{
    char line[256];
    wsprintfA(line, "  thresholds: cpu %u permille | disk %u B/s | net %u B/s"
                    " | mem %u faults/s | wait %us",
              g_cfg.ch[CH_CPU].threshold, g_cfg.ch[CH_DISK].threshold,
              g_cfg.ch[CH_NET].threshold, g_cfg.ch[CH_MEM].threshold);
    log_line(line);
}

static void stop_watching(const WCHAR *why)
{
    if (g_mode == MODE_WATCH) log_line("stopped watching; lock released");
    power_release();
    tray_remove();
    g_mode = MODE_PICK;
    g_state = ACT_IDLE;
    g_watch_pid = 0;
    ui_list_set_enabled(g_list, 1);
    wcopy(g_notice, why ? why : L"", 128);
    /* The sampler is left running: the app is still selected, and its
       readings are as useful now as they were before the button was pressed.
       refresh_apps points it wherever the selection ends up. */
    refresh_apps();
    InvalidateRect(g_wnd, 0, FALSE);
}

static void start_watching(void)
{
    const AppEntry *a;

    if (g_list_state.sel < 0 || g_list_state.sel >= g_app_count) return;
    a = &g_apps[g_list_state.sel];

    /* Usually already sampling this very process, in which case the rolling
       windows it has built are kept -- the rule gets to start with history
       rather than from nothing. */
    if (!g_sampling || !g_monitor.alive || g_monitor.root_pid != a->pid) {
        if (!monitor_start(&g_monitor, a->pid)) {
            wcopy(g_notice, L"that app closed before we could attach to it", 128);
            refresh_apps();
            return;
        }
        activity_init(&g_activity, &g_cfg);
        g_sampling = 1;
    }

    wcopy(g_watch_title, a->title, 128);
    wcopy(g_watch_exe, a->exe, 64);
    g_watch_pid = a->pid;
    g_notice[0] = 0;

    g_mode = MODE_WATCH;
    /* Hold immediately: the user pressed the button because work is running
       right now, and waiting a whole tick to find out would look broken. */
    g_state = ACT_BUSY;
    power_apply(1, g_keep_display);

    ui_list_set_enabled(g_list, 0);
    if (!tray_add()) {
        char line[128];
        wsprintfA(line, "  the shell refused the tray icon: error %u, "
                        "struct %u bytes",
                  tray_last_error(), tray_struct_size());
        log_line(line);
    }
    update_tray();

    {
        char line[128];
        log_linew("watching: ", a->title);
        wsprintfA(line, "  pid %u, plus every process it spawns", a->pid);
        log_line(line);
        log_linew("  image: ", a->exe);
        log_thresholds();
        log_line(g_keep_display ? "  screen will be kept on too"
                                : "  screen may still turn off");
    }

    SetTimer(g_wnd, TIMER_TICK, CFG_POLL_MS, 0);
    ShowWindow(g_wnd, SW_HIDE);
}

static void show_main_window(void)
{
    ShowWindow(g_wnd, SW_SHOW);
    SetForegroundWindow(g_wnd);
    if (g_mode == MODE_PICK) refresh_apps();
    InvalidateRect(g_wnd, 0, FALSE);
}

/* ------------------------------------------------------------------ tick */

/* One line per tick, with every number the rule looked at. This is what
   turns "it never lets my machine sleep" into something you can read.
   A trailing r marks a bar the app's own baseline raised. */
static void log_tick(const TrackerDelta *d, int conns, int held)
{
    char line[400];
    const Activity *a = &g_activity;

    wsprintfA(line,
        "tick procs=%d cpu=%u/%u%s disk=%u/%u%s net=%u/%u%s mem=%u/%u%s "
        "ws=%uMB conn=%d -> %s (%s) quiet=%us lock=%s",
        d->n_procs,
        a->ch[CH_CPU].smoothed,  a->ch[CH_CPU].bar,  a->ch[CH_CPU].learned_used  ? "r" : "",
        a->ch[CH_DISK].smoothed, a->ch[CH_DISK].bar, a->ch[CH_DISK].learned_used ? "r" : "",
        a->ch[CH_NET].smoothed,  a->ch[CH_NET].bar,  a->ch[CH_NET].learned_used  ? "r" : "",
        a->ch[CH_MEM].smoothed,  a->ch[CH_MEM].bar,  a->ch[CH_MEM].learned_used  ? "r" : "",
        (unsigned int)(a->ws_bytes >> 20),
        conns,
        g_state == ACT_BUSY ? "BUSY" : g_state == ACT_GRACE ? "grace" : "IDLE",
        activity_channel_name(a->why),
        (unsigned)(a->quiet_ms / 1000),
        held ? (power_ok() ? "held" : "REFUSED") : "off");

    log_line(line);
}

static void on_tick(void)
{
    TrackerDelta d;
    ActivityState before = g_state;
    int conns, held;

    if (!monitor_tick(&g_monitor, &d)) {
        if (g_mode == MODE_WATCH) {
            log_line("the watched process exited");
            stop_watching(L"that app has closed \x00b7 the lock was released");
            show_main_window();
        } else {
            /* Only ever showing readings for it, so nothing to release. */
            sample_app(0);
            refresh_apps();
        }
        return;
    }

    conns = netstat_established(g_monitor.pids, g_monitor.pid_count);
    g_state = activity_update(&g_activity, &d, conns);

    /* Everything above happens either way. What follows is the part that
       touches the machine, and it belongs to watching only. */
    if (g_mode == MODE_WATCH) {
        held = activity_should_hold(g_state);
        power_apply(held, g_keep_display);
        update_tray();

        log_tick(&d, conns, held);
        if (g_state != before) {
            char msg[96];
            wsprintfA(msg, "  ^ state changed: %s -> %s",
                      before == ACT_BUSY ? "BUSY" : before == ACT_GRACE ? "grace" : "IDLE",
                      g_state == ACT_BUSY ? "BUSY" : g_state == ACT_GRACE ? "grace" : "IDLE");
            log_line(msg);
        }
    }

    if (IsWindowVisible(g_wnd)) InvalidateRect(g_wnd, 0, FALSE);
}

/* ------------------------------------------------------------- tray menu */

static void show_tray_menu(void)
{
    HMENU menu = CreatePopupMenu();
    POINT pt;

    if (!menu) return;

    AppendMenuW(menu, MF_STRING, MENU_SHOW, L"Show nosleep");
    if (g_mode == MODE_WATCH)
        AppendMenuW(menu, MF_STRING, MENU_RELEASE, L"Release the lock");
    AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    AppendMenuW(menu, MF_STRING, MENU_EXIT, L"Exit");

    GetCursorPos(&pt);
    /* Required so the menu closes when the user clicks elsewhere. */
    SetForegroundWindow(g_wnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_wnd, 0);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

/* ---------------------------------------------------------- window proc */

/* Moving a slider takes effect at once, including mid-watch: the point of
   having it is to see the decision change while you drag. */
static void slider_changed(int i)
{
    ChannelRule *r = (i == SL_WAIT) ? &g_cfg.wait : &g_cfg.ch[i];
    r->threshold = activity_from_slider(r, g_slider[i].pos);
    if (i == SL_WAIT) g_activity.cfg.wait = g_cfg.wait;
    else              g_activity.cfg.ch[i] = g_cfg.ch[i];
    InvalidateRect(g_wnd, 0, FALSE);
}

static void on_mouse_move(int x, int y)
{
    RECT btn = button_rect(), chk = check_rect(), ref = refresh_rect();
    HotItem was = g_hot;
    int i, hot_changed = 0;

    if (g_dragging >= 0) {
        if (ui_slider_drag(&g_slider[g_dragging], x))
            slider_changed(g_dragging);
        return;
    }

    for (i = 0; i < SL_COUNT; i++) {
        int h = ui_slider_hit(&g_slider[i], x, y);
        if (h != g_slider[i].hot) { g_slider[i].hot = h; hot_changed = 1; }
    }

    if (in_rect(&btn, x, y)) g_hot = HOT_BUTTON;
    else if (in_rect(&chk, x, y)) g_hot = HOT_CHECK;
    else if (g_mode == MODE_PICK && in_rect(&ref, x, y)) g_hot = HOT_REFRESH;
    else g_hot = HOT_NONE;

    if (g_hot != was || hot_changed) {
        TRACKMOUSEEVENT tme;
        InvalidateRect(g_wnd, 0, FALSE);
        tme.cbSize = sizeof tme;
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = g_wnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);
    }
}

static void on_click(HotItem item)
{
    switch (item) {
    case HOT_BUTTON:
        if (g_mode == MODE_WATCH)
            stop_watching(L"released \x00b7 pick another app when you need to");
        else
            start_watching();
        break;

    case HOT_CHECK:
        g_keep_display = !g_keep_display;
        settings_save(&g_cfg, g_keep_display);
        if (g_mode == MODE_WATCH)
            power_apply(activity_should_hold(g_state), g_keep_display);
        break;

    case HOT_REFRESH:
        g_notice[0] = 0;
        refresh_apps();
        break;

    default:
        break;
    }
    InvalidateRect(g_wnd, 0, FALSE);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    static UINT taskbar_created;

    if (taskbar_created && msg == taskbar_created) {
        /* Explorer restarted and took every tray icon with it. */
        if (g_mode == MODE_WATCH) { tray_add(); update_tray(); }
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        /* Registered here rather than read back from tray.c: WM_CREATE
           arrives from inside CreateWindowExW, before tray_init has run. */
        taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        return 0;

    case WM_PAINT:
        paint_window(h);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
        on_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MOUSELEAVE:
        if (g_hot != HOT_NONE) { g_hot = HOT_NONE; InvalidateRect(h, 0, FALSE); }
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int i;
        for (i = 0; i < SL_COUNT; i++) {
            if (ui_slider_hit(&g_slider[i], x, y)) {
                g_dragging = i;
                SetCapture(h);
                if (ui_slider_click(&g_slider[i], x, y)) slider_changed(i);
                else InvalidateRect(h, 0, FALSE);
                return 0;
            }
        }
        on_mouse_move(x, y);
        if (g_hot != HOT_NONE) {
            g_pressed = g_hot;
            SetCapture(h);
            InvalidateRect(h, 0, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        HotItem was = g_pressed;
        if (g_dragging >= 0) {
            g_slider[g_dragging].dragging = 0;
            g_dragging = -1;
            ReleaseCapture();
            /* Written once the grip is let go, not on every pixel. */
            settings_save(&g_cfg, g_keep_display);
            if (g_mode == MODE_WATCH) log_thresholds();
            InvalidateRect(h, 0, FALSE);
            return 0;
        }
        if (was != HOT_NONE) {
            ReleaseCapture();
            g_pressed = HOT_NONE;
            on_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (g_hot == was) on_click(was);
        }
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == ID_LIST) {
            if (HIWORD(wp) == UILN_ACTIVATE) {
                start_watching();
            } else {
                /* Point the sampler at whatever was just picked, so the
                   readings appear straight away rather than only once the
                   button is pressed. */
                sample_app(g_list_state.sel >= 0
                               ? g_apps[g_list_state.sel].pid : 0);
                InvalidateRect(h, 0, FALSE);    /* the button may light up */
            }
            return 0;
        }
        switch (LOWORD(wp)) {
        case MENU_SHOW:    show_main_window(); return 0;
        case MENU_RELEASE: stop_watching(L"released from the tray");
                           show_main_window(); return 0;
        case MENU_EXIT:    DestroyWindow(h); return 0;
        }
        return 0;

    case MSG_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK)
            show_main_window();
        else if (LOWORD(lp) == WM_RBUTTONUP)
            show_tray_menu();
        return 0;

    case MSG_SHOW_ME:
        show_main_window();
        return 0;

    case WM_TIMER:
        if (wp == TIMER_TICK) on_tick();
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && g_mode == MODE_PICK) refresh_apps();
        return 0;

    case WM_SETTINGCHANGE:
        /* Covers the user flipping the system light/dark setting. */
        theme_load(&g_theme);
        theme_apply_titlebar(h, g_theme.dark);
        InvalidateRect(h, 0, TRUE);
        InvalidateRect(g_list, 0, TRUE);
        return 0;

    case WM_CLOSE:
        /* While monitoring, closing the window means "get out of my way",
           not "stop". The tray icon is what keeps that honest. */
        if (g_mode == MODE_WATCH) { ShowWindow(h, SW_HIDE); return 0; }
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        KillTimer(h, TIMER_TICK);
        power_release();
        log_line("nosleep exited; lock released");
        log_close();
        netstat_shutdown();
        tray_shutdown();
        if (g_icon_small) DestroyIcon(g_icon_small);
        if (g_icon_big) DestroyIcon(g_icon_big);
        ui_free_fonts();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(h, msg, wp, lp);
}

/* ---------------------------------------------------------------- WinMain */

static HWND create_main_window(void)
{
    WNDCLASSEXW wc;
    RECT want, work;
    DWORD style = WIN_STYLE;
    int w, ht, x, y;
    int i;
    unsigned char *p = (unsigned char *)&wc;
    HWND h;

    for (i = 0; i < (int)sizeof wc; i++) p[i] = 0;
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = (WNDPROC)wnd_proc;   /* TCC compares __stdcall types loosely */
    wc.hInstance = g_inst;
    /* TCC defines IDC_ARROW via the ANSI MAKEINTRESOURCE; the value is a
       numeric id, so the cast is the whole fix. */
    wc.hCursor = LoadCursorW(0, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = CFG_WND_CLASS;
    if (!RegisterClassExW(&wc)) return 0;

    want.left = 0;
    want.top = 0;
    want.right = ui_scale(WIN_W);
    want.bottom = g_client_h;
    AdjustWindowRect(&want, style, FALSE);
    w = want.right - want.left;
    ht = want.bottom - want.top;

    /* Centred in the work area rather than left to the default cascade,
       which on a short screen drops the button behind the taskbar. */
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.left = 0;
        work.top = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    x = work.left + ((work.right - work.left) - w) / 2;
    y = work.top + ((work.bottom - work.top) - ht) / 2;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;

    h = CreateWindowExW(0, CFG_WND_CLASS, CFG_APP_NAME, style,
                        x, y, w, ht, 0, 0, g_inst, 0);
    return h;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    MSG msg;
    HANDLE once;
    RECT lr;
    int i;

    (void)prev; (void)cmd; (void)show;
    g_inst = inst;

    /* One instance. A second launch just brings the first one forward. */
    once = CreateMutexW(0, TRUE, CFG_MUTEX_NAME);
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(CFG_WND_CLASS, 0);
        if (other) {
            PostMessageW(other, MSG_SHOW_ME, 0, 0);
            SetForegroundWindow(other);
        }
        return 0;
    }

    SetProcessDPIAware();
    log_open();
    log_line("--------------------------------------------------------");
    log_line("nosleep started");
    netstat_init();
    ui_init_metrics();
    compute_layout();
    theme_load(&g_theme);

    activity_defaults(&g_cfg);
    settings_load(&g_cfg, &g_keep_display);
    log_thresholds();
    for (i = 0; i < SL_COUNT; i++) {
        const ChannelRule *r = (i == SL_WAIT) ? &g_cfg.wait : &g_cfg.ch[i];
        g_slider[i].pos = activity_to_slider(r, r->threshold);
        g_slider[i].hot = 0;
        g_slider[i].dragging = 0;
    }
    activity_init(&g_activity, &g_cfg);

    ui_register_list_class(inst);

    g_wnd = create_main_window();
    if (!g_wnd) return 1;

    theme_apply_titlebar(g_wnd, g_theme.dark);

    /* Two sizes: the small one is the title bar and Alt+Tab, the big one is
       the taskbar and the task switcher. Drawn, not linked in, because TCC
       has no resource compiler. */
    g_icon_small = icon_make(ICON_BRAND, GetSystemMetrics(SM_CXSMICON), 1);
    g_icon_big   = icon_make(ICON_BRAND, GetSystemMetrics(SM_CXICON), 1);
    if (g_icon_small)
        SendMessageW(g_wnd, WM_SETICON, ICON_SMALL, (LPARAM)g_icon_small);
    if (g_icon_big)
        SendMessageW(g_wnd, WM_SETICON, ICON_BIG, (LPARAM)g_icon_big);

    tray_init(g_wnd, MSG_TRAY);
    place_sliders();

    g_list_state.theme = &g_theme;
    g_list_state.draw_row = draw_row;
    g_list_state.row_h = ui_scale(ROW_H);
    g_list = ui_create_list(g_wnd, ID_LIST, &g_list_state);

    lr = rect_at(PAD, ui_scale(LIST_Y), WIN_W - 2 * PAD, 0);
    lr.bottom = lr.top + g_list_h;
    MoveWindow(g_list, lr.left, lr.top, lr.right - lr.left,
               lr.bottom - lr.top, FALSE);

    refresh_apps();
    ShowWindow(g_wnd, SW_SHOW);
    UpdateWindow(g_wnd);

    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (once) CloseHandle(once);
    return 0;
}
