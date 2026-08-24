#ifndef WAKEWHILE_ICON_H
#define WAKEWHILE_ICON_H

#include "wincompat.h"

/* The application mark: an eye, open while the lock is held and closed while
   it is not.

   Drawn into a DIB at run time rather than compiled in as a resource, because
   TinyCC has no resource compiler. Everything is integer arithmetic with 4x4
   coverage sampling and the geometry is in percent of the box, so one set of
   numbers serves the 16 px title bar, the 32 px taskbar, and the 256 px
   entry Explorer wants -- with no second asset to keep in step.

   tools/seticon.c uses the same renderer to write a real icon resource into
   the finished executable, which is what Explorer reads. */

/* `px` must hold size*size pixels, premultiplied BGRA, top down.
   `open` picks the open eye over the closed one. */
void icon_render(unsigned char *px, int size, COLORREF colour, int open);

/* Caller owns the result and must DestroyIcon it. */
HICON icon_make(COLORREF colour, int size, int open);

/* The blue used for the window, the taskbar and the file itself, chosen to
   hold up against both a light and a dark background. */
#define ICON_BRAND RGB(0x3B, 0x9E, 0xF5)

#endif
