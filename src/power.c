#include "power.h"

static DWORD g_flags = ES_CONTINUOUS;   /* what we last asked for */
static int   g_ok = 1;

void power_apply(int hold, int keep_display)
{
    DWORD want = ES_CONTINUOUS;

    if (hold) {
        want |= ES_SYSTEM_REQUIRED;
        if (keep_display) want |= ES_DISPLAY_REQUIRED;
    }

    if (want == g_flags) return;

    /* ES_CONTINUOUS makes the request stick until we change it, rather than
       counting as a single "the user is here" nudge. */
    g_ok = SetThreadExecutionState(want) != 0;
    g_flags = want;
}

int power_ok(void)
{
    return g_ok;
}

void power_release(void)
{
    if (g_flags == ES_CONTINUOUS) return;
    SetThreadExecutionState(ES_CONTINUOUS);
    g_flags = ES_CONTINUOUS;
}
