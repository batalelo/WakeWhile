#ifndef NOSLEEP_SETTINGS_H
#define NOSLEEP_SETTINGS_H

/* Where the sliders were left, kept in a plain text file beside the
   executable so it can be read and edited by hand. */

#include "wincompat.h"
#include "activity.h"

void settings_load(ActivityConfig *cfg, int *keep_display);
void settings_save(const ActivityConfig *cfg, int keep_display);

#endif
