#ifndef NOSLEEP_TRAY_H
#define NOSLEEP_TRAY_H

#include "wincompat.h"
#include "config.h"

/* The tray icon is drawn into a DIB at run time -- a coloured dot -- because
   TCC has no resource compiler and we want a single file with nothing beside
   it. */

typedef enum {
    TRAY_HOLDING = 0,   /* green  */
    TRAY_GRACE   = 1,   /* amber  */
    TRAY_OFF     = 2    /* grey   */
} TrayState;

void tray_init(HWND owner, UINT callback_msg);
void tray_add(void);
void tray_remove(void);
void tray_set(TrayState state, const WCHAR *tip);
void tray_shutdown(void);

/* Explorer restarting takes every tray icon with it. */
UINT tray_restart_message(void);

#endif
