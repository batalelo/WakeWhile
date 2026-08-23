#ifndef NOSLEEP_LOGFILE_H
#define NOSLEEP_LOGFILE_H

/* A plain text log written next to the executable, so that "why is my
   machine still awake" has an answer you can read rather than guess at.

   Opened shared, so it can be tailed while nosleep is running. */

#include "wincompat.h"

void log_open(void);
void log_close(void);

/* Where the file ended up, for the UI to show. Empty if it could not be
   opened -- a read-only folder, most likely. */
const WCHAR *log_path(void);

/* One timestamped line. `text` is ASCII; use log_linew for anything that may
   contain an app title. */
void log_line(const char *text);
void log_linew(const char *prefix, const WCHAR *text);

#endif
