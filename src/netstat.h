#ifndef WAKEWHILE_NETSTAT_H
#define WAKEWHILE_NETSTAT_H

/* How many established TCP connections to somewhere off this machine the
   watched process tree currently holds.

   Windows will not give an unprivileged process a per-process byte count for
   network traffic -- the only APIs that do need an elevated prompt. What it
   will give us is the connection table, which is enough to answer the
   question the rule actually asks: is this app talking to the outside world
   at all? That gates the noisier byte counter, so IPC chatter from an app
   with nothing open is never mistaken for a download. */

#include "wincompat.h"

void netstat_init(void);

/* `pids` is the tree. Returns the number of ESTABLISHED connections whose
   remote end is neither loopback nor unspecified. Counts IPv4 and IPv6.
   Returns 0 if iphlpapi is unavailable. */
int netstat_established(const unsigned int *pids, int n);

void netstat_shutdown(void);

#endif
