/* Trace-mode hooks for execution, redirection, and child status events. */
#ifndef TRACE_H
#define TRACE_H

#include <sys/types.h>

#include "command.h"

typedef struct Shell Shell;

/* Set trace mode on or off. */
void trace_set_enabled(Shell* shell, int enabled);
/* Return whether trace mode is enabled. */
int trace_is_enabled(Shell* shell);

/* Print trace mode status. */
void trace_print_status(Shell* shell);

/* Log pipe creation. */
void trace_pipe_created(Shell* shell, int read_fd, int write_fd);
/* Log a fork event. */
void trace_fork(Shell* shell, pid_t pid, Command* cmd);
/* Log an exec event. */
void trace_exec(Shell* shell, Command* cmd);
/* Log a dup2 event. */
void trace_dup2(Shell* shell, int old_fd, int new_fd, const char* reason);
/* Log a redirection event. */
void trace_redirection(Shell* shell, const char* filename, int target_fd);
/* Log child process completion. */
void trace_child_exit(Shell* shell, pid_t pid, int status);

#endif