/* Main shell state and event loop interface. */
#ifndef SHELL_H
#define SHELL_H

#include <sys/types.h>

#include "process.h"

#define PROCSH_PROMPT "procsh> "

typedef struct Shell {
    int running;
    int last_status;
    int trace_enabled;
    int terminal_fd;
    pid_t shell_pgid;
    ProcessTable process_table;
} Shell;

/* Initialize shell runtime state. */
void shell_init(Shell* shell);
/* Run the interactive shell loop. */
void shell_loop(Shell* shell);
/* Release shell resources. */
void shell_destroy(Shell* shell);

#endif