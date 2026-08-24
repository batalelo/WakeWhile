#ifndef WAKEWHILE_UI_H
#define WAKEWHILE_UI_H

#include "wincompat.h"
#include "theme.h"

/* Drawing primitives and one custom list control.

   Nothing here uses comctl32. TCC cannot embed an application manifest, so
   the common controls would fall back to the Windows 95 look; drawing the
   list ourselves avoids that entirely, removes a DLL dependency, and gives
   us a scrollbar that follows the dark theme. */

/* ------------------------------------------------------------------ DPI */

void ui_init_metrics(void);
int  ui_dpi(void);
int  ui_scale(int logical_px);

/* ---------------------------------------------------------------- fonts */

typedef struct {
    HFONT head;    /* the one-line explanation at the top */
    HFONT body;    /* row titles, checkbox                */
    HFONT small;   /* row subtitles, status               */
    HFONT button;  /* the main button                     */
} UiFonts;

const UiFonts *ui_fonts(void);
void ui_free_fonts(void);

/* ----------------------------------------------------------- primitives */

void ui_fill(HDC dc, const RECT *r, COLORREF c);
void ui_fill_round(HDC dc, const RECT *r, int radius, COLORREF c);
void ui_frame_round(HDC dc, const RECT *r, int radius, COLORREF c);
void ui_text(HDC dc, const RECT *r, const WCHAR *s, HFONT f, COLORREF c,
             UINT format);

/* ---------------------------------------------------------------- list */

#define UI_LIST_CLASS  L"WakeWhileListView"

/* Notification codes delivered to the parent as WM_COMMAND. */
#define UILN_SELCHANGE 1
#define UILN_ACTIVATE  2

/* Draws one row into `r`. The background has already been filled. */
typedef void (*UiListDrawRow)(HDC dc, int index, const RECT *r,
                              int selected, int hot, void *user);

typedef struct {
    const Theme  *theme;
    UiListDrawRow draw_row;
    void         *user;
    int count;
    int row_h;
    int sel;         /* -1 for none                          */
    int hot;         /* -1 for none                          */
    int top_px;      /* scroll offset in pixels              */
    int enabled;
    int dragging;    /* the scrollbar thumb is being dragged */
    int drag_grab;   /* cursor offset inside the thumb       */
    int ctrl_id;
} UiList;

void ui_register_list_class(HINSTANCE inst);

/* `state` must outlive the window. */
HWND ui_create_list(HWND parent, int id, UiList *state);

void ui_list_set_count(HWND list, int count);
void ui_list_set_enabled(HWND list, int enabled);
void ui_list_reveal(HWND list, int index);


/* --------------------------------------------------------------- slider */

/* A horizontal track with a round grip, drawn like everything else here.
   The parent gets WM_COMMAND with UISN_CHANGED while it is dragged.

   Sliders are not separate windows: there are four of them and they live in
   the main window's paint and hit-test, which is less code than four child
   windows and keeps the whole panel on one back buffer. */

#define UISN_CHANGED 3

typedef struct {
    RECT track;        /* set by the layout each paint  */
    unsigned int pos;  /* 0..1000                       */
    int hot;
    int dragging;
} UiSlider;

void ui_draw_slider(HDC dc, const Theme *t, const UiSlider *s, int enabled);

/* Returns 1 if the position changed. `x` is in the same space as the track. */
int ui_slider_click(UiSlider *s, int x, int y);
int ui_slider_drag(UiSlider *s, int x);
int ui_slider_hit(const UiSlider *s, int x, int y);

#endif
