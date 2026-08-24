#include "ui.h"

static int g_dpi = 96;
static UiFonts g_fonts;
static int g_fonts_ready;

/* ------------------------------------------------------------------ DPI */

void ui_init_metrics(void)
{
    HDC dc = GetDC(0);
    if (dc) {
        int d = GetDeviceCaps(dc, LOGPIXELSX);
        if (d >= 72) g_dpi = d;
        ReleaseDC(0, dc);
    }
}

int ui_dpi(void) { return g_dpi; }

int ui_scale(int logical_px) { return MulDiv(logical_px, g_dpi, 96); }

/* ---------------------------------------------------------------- fonts */

static HFONT make_font(int pt, int weight)
{
    /* Segoe UI is present from Vista on; the fallback matters only in the
       unlikely event it is missing, and Windows will substitute anyway. */
    return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Segoe UI");
}

const UiFonts *ui_fonts(void)
{
    if (!g_fonts_ready) {
        g_fonts.head   = make_font(11, FW_NORMAL);
        g_fonts.body   = make_font(9,  FW_NORMAL);
        g_fonts.small  = make_font(8,  FW_NORMAL);
        g_fonts.button = make_font(11, FW_SEMIBOLD);
        g_fonts_ready = 1;
    }
    return &g_fonts;
}

void ui_free_fonts(void)
{
    if (!g_fonts_ready) return;
    DeleteObject(g_fonts.head);
    DeleteObject(g_fonts.body);
    DeleteObject(g_fonts.small);
    DeleteObject(g_fonts.button);
    g_fonts_ready = 0;
}

/* ----------------------------------------------------------- primitives */

void ui_fill(HDC dc, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, r, b);
    DeleteObject(b);
}

void ui_fill_round(HDC dc, const RECT *r, int radius, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    /* RoundRect excludes its right and bottom edge; +1 makes the filled area
       match the rectangle the caller asked for. */
    RoundRect(dc, r->left, r->top, r->right + 1, r->bottom + 1,
              radius * 2, radius * 2);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(b);
}

void ui_frame_round(HDC dc, const RECT *r, int radius, COLORREF c)
{
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HGDIOBJ op = SelectObject(dc, p);
    RoundRect(dc, r->left, r->top, r->right, r->bottom,
              radius * 2, radius * 2);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(p);
}

void ui_text(HDC dc, const RECT *r, const WCHAR *s, HFONT f, COLORREF c,
             UINT format)
{
    RECT t = *r;
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &t, format);
    SelectObject(dc, of);
}

/* ---------------------------------------------------------------- list */

#define SCROLLBAR_W   10
#define THUMB_W        4
#define THUMB_MIN     24

static UiList *list_state(HWND h)
{
    return (UiList *)GetWindowLongPtrW(h, GWLP_USERDATA);
}

static int list_view_h(HWND h)
{
    RECT rc;
    GetClientRect(h, &rc);
    return rc.bottom - rc.top;
}

static int list_content_h(const UiList *s)
{
    return s->count * s->row_h;
}

static int list_max_top(HWND h, const UiList *s)
{
    int over = list_content_h(s) - list_view_h(h);
    return over > 0 ? over : 0;
}

static void list_clamp(HWND h, UiList *s)
{
    int max = list_max_top(h, s);
    if (s->top_px > max) s->top_px = max;
    if (s->top_px < 0) s->top_px = 0;
}

static int list_needs_bar(HWND h, const UiList *s)
{
    return list_content_h(s) > list_view_h(h);
}

/* Geometry of the thumb, in client coordinates. */
static void list_thumb(HWND h, const UiList *s, int *y, int *height)
{
    int view = list_view_h(h);
    int content = list_content_h(s);
    int max = list_max_top(h, s);
    int th, ty;

    th = (content > 0) ? (view * view) / content : view;
    if (th < ui_scale(THUMB_MIN)) th = ui_scale(THUMB_MIN);
    if (th > view) th = view;

    ty = (max > 0) ? ((view - th) * s->top_px) / max : 0;

    *y = ty;
    *height = th;
}

static int list_hit_row(HWND h, const UiList *s, int x, int y)
{
    RECT rc;
    int index;

    GetClientRect(h, &rc);
    if (list_needs_bar(h, s) && x >= rc.right - ui_scale(SCROLLBAR_W))
        return -1;
    if (s->row_h <= 0) return -1;

    index = (y + s->top_px) / s->row_h;
    if (index < 0 || index >= s->count) return -1;
    return index;
}

static void list_paint(HWND h, UiList *s)
{
    PAINTSTRUCT ps;
    HDC dc, mem;
    HBITMAP bmp, old_bmp;
    RECT rc;
    int w, ht, first, last, i;

    dc = BeginPaint(h, &ps);
    GetClientRect(h, &rc);
    w = rc.right;
    ht = rc.bottom;

    /* Double buffered: the rows repaint on every hover and on every scroll
       step, and flicker at that rate is very visible. */
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, w, ht);
    old_bmp = (HBITMAP)SelectObject(mem, bmp);

    ui_fill(mem, &rc, s->theme->panel);

    if (s->row_h > 0 && s->count > 0) {
        first = s->top_px / s->row_h;
        last = (s->top_px + ht) / s->row_h;
        if (last >= s->count) last = s->count - 1;

        for (i = first; i <= last; i++) {
            RECT r;
            int selected = (i == s->sel);
            int hot = (i == s->hot) && s->enabled;

            r.left = 0;
            r.right = w - (list_needs_bar(h, s) ? ui_scale(SCROLLBAR_W) : 0);
            r.top = i * s->row_h - s->top_px;
            r.bottom = r.top + s->row_h;

            if (selected) {
                RECT fillr = r;
                fillr.left += ui_scale(3);
                fillr.right -= ui_scale(3);
                ui_fill_round(mem, &fillr, ui_scale(5), s->theme->row_sel);
            } else if (hot) {
                RECT fillr = r;
                fillr.left += ui_scale(3);
                fillr.right -= ui_scale(3);
                ui_fill_round(mem, &fillr, ui_scale(5), s->theme->row_hot);
            }

            if (s->draw_row) s->draw_row(mem, i, &r, selected, hot, s->user);
        }
    }

    if (list_needs_bar(h, s)) {
        RECT t;
        int ty, th, bx;
        list_thumb(h, s, &ty, &th);
        bx = w - ui_scale(SCROLLBAR_W);
        t.left = bx + (ui_scale(SCROLLBAR_W) - ui_scale(THUMB_W)) / 2;
        t.right = t.left + ui_scale(THUMB_W);
        t.top = ty + ui_scale(2);
        t.bottom = ty + th - ui_scale(2);
        if (t.bottom > t.top)
            ui_fill_round(mem, &t, ui_scale(THUMB_W) / 2, s->theme->scroll);
    }

    BitBlt(dc, 0, 0, w, ht, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

static void list_notify(HWND h, const UiList *s, int code)
{
    HWND parent = GetParent(h);
    if (parent)
        SendMessageW(parent, WM_COMMAND,
                     MAKEWPARAM(s->ctrl_id, code), (LPARAM)h);
}

static void list_select(HWND h, UiList *s, int index)
{
    if (index == s->sel) return;
    s->sel = index;
    ui_list_reveal(h, index);
    InvalidateRect(h, 0, FALSE);
    list_notify(h, s, UILN_SELCHANGE);
}

static void list_scroll_by(HWND h, UiList *s, int delta_px)
{
    int before = s->top_px;
    s->top_px += delta_px;
    list_clamp(h, s);
    if (s->top_px != before) InvalidateRect(h, 0, FALSE);
}

static void list_move_sel(HWND h, UiList *s, int delta)
{
    int n;
    if (s->count == 0) return;
    n = s->sel < 0 ? (delta > 0 ? 0 : s->count - 1) : s->sel + delta;
    if (n < 0) n = 0;
    if (n >= s->count) n = s->count - 1;
    list_select(h, s, n);
}

static LRESULT CALLBACK list_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    UiList *s = list_state(h);

    if (!s) return DefWindowProcW(h, msg, wp, lp);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;   /* WM_PAINT covers every pixel */

    case WM_PAINT:
        list_paint(h, s);
        return 0;

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (s->dragging) {
            int view = list_view_h(h), max = list_max_top(h, s);
            int ty, th;
            list_thumb(h, s, &ty, &th);
            if (view > th) {
                s->top_px = ((y - s->drag_grab) * max) / (view - th);
                list_clamp(h, s);
                InvalidateRect(h, 0, FALSE);
            }
        } else if (s->enabled) {
            int hot = list_hit_row(h, s, x, y);
            if (hot != s->hot) {
                TRACKMOUSEEVENT tme;
                s->hot = hot;
                InvalidateRect(h, 0, FALSE);
                tme.cbSize = sizeof tme;
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = h;
                tme.dwHoverTime = 0;
                TrackMouseEvent(&tme);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (s->hot != -1) { s->hot = -1; InvalidateRect(h, 0, FALSE); }
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        RECT rc;
        if (!s->enabled) return 0;
        SetFocus(h);
        GetClientRect(h, &rc);
        if (list_needs_bar(h, s) && x >= rc.right - ui_scale(SCROLLBAR_W)) {
            int ty, th;
            list_thumb(h, s, &ty, &th);
            if (y >= ty && y < ty + th) {
                s->dragging = 1;
                s->drag_grab = y - ty;
                SetCapture(h);
            } else {
                /* Clicking the track pages towards the cursor. */
                list_scroll_by(h, s, y < ty ? -list_view_h(h)
                                            : list_view_h(h));
            }
        } else {
            int index = list_hit_row(h, s, x, y);
            if (index >= 0) list_select(h, s, index);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (s->dragging) { s->dragging = 0; ReleaseCapture(); }
        return 0;

    case WM_LBUTTONDBLCLK: {
        int index;
        if (!s->enabled) return 0;
        index = list_hit_row(h, s, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (index >= 0) {
            list_select(h, s, index);
            list_notify(h, s, UILN_ACTIVATE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int notches = (short)HIWORD(wp) / WHEEL_DELTA;
        if (s->enabled) list_scroll_by(h, s, -notches * s->row_h * 3);
        return 0;
    }

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;

    case WM_KEYDOWN:
        if (!s->enabled) return 0;
        switch (wp) {
        case VK_UP:    list_move_sel(h, s, -1); return 0;
        case VK_DOWN:  list_move_sel(h, s, +1); return 0;
        case VK_PRIOR: list_move_sel(h, s, -(list_view_h(h) / s->row_h)); return 0;
        case VK_NEXT:  list_move_sel(h, s, +(list_view_h(h) / s->row_h)); return 0;
        case VK_HOME:  list_select(h, s, 0); return 0;
        case VK_END:   list_select(h, s, s->count - 1); return 0;
        case VK_RETURN:
        case VK_SPACE:
            if (s->sel >= 0) list_notify(h, s, UILN_ACTIVATE);
            return 0;
        }
        return 0;

    case WM_SIZE:
        list_clamp(h, s);
        InvalidateRect(h, 0, FALSE);
        return 0;
    }

    return DefWindowProcW(h, msg, wp, lp);
}

void ui_register_list_class(HINSTANCE inst)
{
    WNDCLASSEXW wc;
    int i;
    unsigned char *p = (unsigned char *)&wc;
    for (i = 0; i < (int)sizeof wc; i++) p[i] = 0;

    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = (WNDPROC)list_proc;  /* TCC compares __stdcall types loosely */
    wc.hInstance = inst;
    /* TCC defines IDC_ARROW via the ANSI MAKEINTRESOURCE; the value is a
       numeric id, so the cast is the whole fix. */
    wc.hCursor = LoadCursorW(0, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = UI_LIST_CLASS;
    RegisterClassExW(&wc);
}

HWND ui_create_list(HWND parent, int id, UiList *state)
{
    HWND h;

    state->sel = -1;
    state->hot = -1;
    state->top_px = 0;
    state->dragging = 0;
    state->enabled = 1;
    state->ctrl_id = id;

    h = CreateWindowExW(0, UI_LIST_CLASS, L"",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, 0, 0);
    if (h) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)state);
    return h;
}

void ui_list_set_count(HWND list, int count)
{
    UiList *s = list_state(list);
    if (!s) return;
    s->count = count;
    if (s->sel >= count) s->sel = count - 1;
    s->hot = -1;
    list_clamp(list, s);
    InvalidateRect(list, 0, FALSE);
}

void ui_list_set_enabled(HWND list, int enabled)
{
    UiList *s = list_state(list);
    if (!s) return;
    s->enabled = enabled;
    if (!enabled) s->hot = -1;
    InvalidateRect(list, 0, FALSE);
}

void ui_list_reveal(HWND list, int index)
{
    UiList *s = list_state(list);
    int top, bottom, view;

    if (!s || index < 0 || index >= s->count) return;

    top = index * s->row_h;
    bottom = top + s->row_h;
    view = list_view_h(list);

    if (top < s->top_px) s->top_px = top;
    else if (bottom > s->top_px + view) s->top_px = bottom - view;

    list_clamp(list, s);
}

/* -------------------------------------------------------------- slider */

#define GRIP   14   /* logical diameter of the grip */
#define RAIL    4   /* logical thickness of the track */

/* The grip's centre can only reach half its own width from each end, so the
   travel is narrower than the track. Everything else follows from that. */
static int slider_travel(const UiSlider *s)
{
    int w = s->track.right - s->track.left - ui_scale(GRIP);
    return w > 0 ? w : 1;
}

static int slider_grip_x(const UiSlider *s)
{
    return s->track.left + ui_scale(GRIP) / 2
           + (int)((s->pos * (unsigned)slider_travel(s)) / 1000);
}

void ui_draw_slider(HDC dc, const Theme *t, const UiSlider *s, int enabled)
{
    RECT rail, filled, grip;
    int cy = (s->track.top + s->track.bottom) / 2;
    int gx = slider_grip_x(s);
    int r = ui_scale(GRIP) / 2;
    COLORREF accent = enabled ? (s->hot || s->dragging ? t->accent_hot
                                                       : t->accent)
                              : t->muted_text;

    rail.left = s->track.left;
    rail.right = s->track.right;
    rail.top = cy - ui_scale(RAIL) / 2;
    rail.bottom = rail.top + ui_scale(RAIL);
    ui_fill_round(dc, &rail, ui_scale(RAIL) / 2,
                  enabled ? t->border : t->muted);

    filled = rail;
    filled.right = gx;
    if (filled.right > filled.left)
        ui_fill_round(dc, &filled, ui_scale(RAIL) / 2, accent);

    grip.left = gx - r;
    grip.right = gx + r;
    grip.top = cy - r;
    grip.bottom = cy + r;
    /* A ring rather than a disc: the panel colour in the middle keeps the
       grip legible against the filled part of the rail. */
    ui_fill_round(dc, &grip, r, accent);
    grip.left += ui_scale(4);
    grip.right -= ui_scale(4);
    grip.top += ui_scale(4);
    grip.bottom -= ui_scale(4);
    ui_fill_round(dc, &grip, (grip.right - grip.left) / 2, t->bg);
}

int ui_slider_hit(const UiSlider *s, int x, int y)
{
    /* Generous vertically: the rail is four pixels and nobody can hit that. */
    return x >= s->track.left - ui_scale(GRIP) / 2 &&
           x <= s->track.right + ui_scale(GRIP) / 2 &&
           y >= s->track.top && y < s->track.bottom;
}

static int slider_set_from_x(UiSlider *s, int x)
{
    int travel = slider_travel(s);
    int rel = x - (s->track.left + ui_scale(GRIP) / 2);
    unsigned int pos;

    if (rel < 0) rel = 0;
    if (rel > travel) rel = travel;
    pos = (unsigned int)(((long long)rel * 1000) / travel);

    if (pos == s->pos) return 0;
    s->pos = pos;
    return 1;
}

int ui_slider_click(UiSlider *s, int x, int y)
{
    if (!ui_slider_hit(s, x, y)) return 0;
    s->dragging = 1;
    return slider_set_from_x(s, x);
}

int ui_slider_drag(UiSlider *s, int x)
{
    if (!s->dragging) return 0;
    return slider_set_from_x(s, x);
}
