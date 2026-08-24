#ifndef WAKEWHILE_THEME_H
#define WAKEWHILE_THEME_H

#include "wincompat.h"

/* Every control is drawn by hand, so following the system light/dark setting
   costs a palette swap and nothing else. */

typedef struct {
    int dark;
    COLORREF bg;          /* window background          */
    COLORREF panel;       /* list background            */
    COLORREF row_hot;     /* hovered row                */
    COLORREF row_sel;     /* selected row               */
    COLORREF text;
    COLORREF text_dim;
    COLORREF text_sel;
    COLORREF border;
    COLORREF accent;      /* the main button            */
    COLORREF accent_hot;
    COLORREF accent_down;
    COLORREF accent_text;
    COLORREF muted;       /* the disabled button        */
    COLORREF muted_text;
    COLORREF scroll;
    COLORREF ok;          /* holding   */
    COLORREF warn;        /* in grace  */
    COLORREF off;         /* released  */
} Theme;

void theme_load(Theme *t);

/* Applies the dark title bar where Windows supports it. */
void theme_apply_titlebar(HWND hwnd, int dark);

#endif
