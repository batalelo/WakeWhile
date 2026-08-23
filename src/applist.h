#ifndef NOSLEEP_APPLIST_H
#define NOSLEEP_APPLIST_H

/* The apps the user currently has open -- the same set Alt+Tab shows. */

#include "wincompat.h"
#include "config.h"

typedef struct {
    unsigned int pid;
    HWND  hwnd;
    WCHAR title[128];
    WCHAR exe[64];
} AppEntry;

/* Fills `out` in z-order, most recently used first, one entry per process.
   Returns the number written. */
int applist_collect(AppEntry *out, int max);

#endif
