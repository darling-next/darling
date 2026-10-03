#ifndef DARLING_ROOTLESS_SHUTDOWN_H
#define DARLING_ROOTLESS_SHUTDOWN_H

#include <sys/types.h>

/* The server must be the rootless prefix's live Linux child subreaper.
 * Success means all guests exited before the pinned server was terminated. */
int shutdown_rootless_process_tree(pid_t server);

#endif
