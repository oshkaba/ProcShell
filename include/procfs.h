/* Lightweight /proc reader used by the psx built-in. */
#ifndef PROCFS_H
#define PROCFS_H

#include <sys/types.h>

#define PROCFS_MAX_NAME 256
#define PROCFS_MAX_STATE 64
#define PROCFS_MAX_CMDLINE 1024

typedef struct ProcInfo {
    pid_t pid;
    char name[PROCFS_MAX_NAME];
    char state[PROCFS_MAX_STATE];
    char cmdline[PROCFS_MAX_CMDLINE];
} ProcInfo;

/* Print a table for all readable processes. */
int procfs_print_all(void);
/* Print a table for one process id. */
int procfs_print_pid(pid_t pid);

#endif