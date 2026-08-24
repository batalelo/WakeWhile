#ifndef NOSLEEP_ICON_H
#define NOSLEEP_ICON_H

#include "wincompat.h"

/* The application mark: a crescent moon with a bar through it.

   Drawn into a DIB at run time rather than compiled in as a resource,
   because TinyCC has no resource compiler and the whole point of this
   program is that it is one file with nothing beside it. Everything is
   integer arithmetic with 4x4 coverage sampling, so it stays crisp at 16 px
   and at 256 px without a second asset. */

/* `px` must hold size*size pixels, premultiplied BGRA, top down. */
void icon_render(unsigned char *px, int size, COLORREF colour);

/* Caller owns the result and must DestroyIcon it. */
HICON icon_make(COLORREF colour, int size);

/* The blue used for the window and taskbar, chosen to hold up against both a
   light and a dark taskbar. */
#define ICON_BRAND RGB(0x3B, 0x9E, 0xF5)

#endif
