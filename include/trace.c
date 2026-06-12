/* Trace logging helpers used when trace mode is enabled. */
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shell.h"
#include "trace.h"
#include "colors.h"

/* Enables or disables trace mode on the shell state. */
void trace_set_enabled(Shell* shell, int enabled)
{
    if(shell == NULL)
    {
        return;
    }

    shell->trace_enabled = enabled;
}

/* Returns whether trace mode is currently active. */
int trace_is_enabled(Shell* shell)
{
    if(shell == NULL)
    {
        return 0;
    }

    return shell->trace_enabled;
}

/* Prints trace mode status for the trace built-in. */
void trace_print_status(Shell* shell)
{
    if(trace_is_enabled(shell))
    {
        printf(COLOR_MAGENTA "trace is on" COLOR_RESET "\n");
    }
    else
    {
        printf(COLOR_MAGENTA "trace is off" COLOR_RESET "\n");
    }
}

/* Logs pipe creation when trace mode is enabled. */
void trace_pipe_created(Shell* shell, int read_fd, int write_fd)
{
    if(!trace_is_enabled(shell))
    {
        return;
    }

    fprintf(stderr, COLOR_MAGENTA "[trace] pipe created: read=%d write=%d" COLOR_RESET "\n", read_fd, write_fd);
}

/* Logs a forked child process and its command name. */
void trace_fork(Shell* shell, pid_t pid, Command* cmd)
{
    if(!trace_is_enabled(shell))
    {
        return;
    }

    if(cmd != NULL && cmd->argc > 0 && cmd->argv != NULL)
    {
        fprintf(stderr, COLOR_MAGENTA "[trace] forked pid=%d for command '%s'" COLOR_RESET "\n", (int)pid, cmd->argv[0]);
    }
    else
    {
        fprintf(stderr, COLOR_MAGENTA "[trace] forked pid=%d" COLOR_RESET "\n", (int)pid);
    }
}

/* Logs the command passed to execvp. */
void trace_exec(Shell* shell, Command* cmd)
{
    if(!trace_is_enabled(shell))
    {
        return;
    }

    if(cmd != NULL && cmd->argc > 0 && cmd->argv != NULL)
    {
        fprintf(stderr, COLOR_MAGENTA "[trace] execvp('%s')" COLOR_RESET "\n", cmd->argv[0]);
    }
    else
    {
        fprintf(stderr, COLOR_MAGENTA "[trace] execvp unknown command" COLOR_RESET "\n");
    }
}

/* Logs descriptor duplication for pipes and redirections. */
void trace_dup2(Shell* shell, int old_fd, int new_fd, const char* reason)
{
    if(!trace_is_enabled(shell))
    {
        return;
    }

    if(reason == NULL)
    {
        reason = "fd redirect";
    }

    fprintf(stderr, COLOR_MAGENTA "[trace] dup2(%d, %d): %s" COLOR_RESET "\n", old_fd, new_fd, reason);
}

/* Logs which standard stream a redirection changes. */
void trace_redirection(Shell* shell, const char* filename, int target_fd)
{
    if(!trace_is_enabled(shell))
    {
        return;
    }

    if(filename == NULL)
    {
        filename = "(null)";
    }

    if(target_fd == STDIN_FILENO)
    {
        fprintf(stderr, COLOR_MAGENTA "[trace] stdin redirected from '%s'" COLOR_RESET "\n", filename);
    }
    else if(target_fd == STDOUT_FILENO)
    {
        fprintf(stderr, COLOR_MAGENTA "[trace] stdout redirected to '%s'" COLOR_RESET "\n", filename);
    }
    else if(target_fd == STDERR_FILENO)
    {
        fprintf(stderr, COLOR_MAGENTA "[trace] stderr redirected to '%s'" COLOR_RESET "\n", filename);
    }
    else
    {
        fprintf(stderr, COLOR_MAGENTA "[trace] fd %d redirected using '%s'" COLOR_RESET "\n", target_fd, filename);
    }
}

/* Logs how a child process finished. */
void trace_child_exit(Shell* shell, pid_t pid, int status)
{
    if(!trace_is_enabled(shell))
    {
        return;
    }

    if(WIFEXITED(status))
    {
        fprintf(stderr, COLOR_MAGENTA "[trace] pid=%d exited with status %d" COLOR_RESET "\n", (int)pid, WEXITSTATUS(status));
    }
    else if(WIFSIGNALED(status))
    {
        fprintf(stderr, COLOR_MAGENTA "[trace] pid=%d killed by signal %d" COLOR_RESET "\n", (int)pid, WTERMSIG(status));
    }
    else if(WIFSTOPPED(status))
    {
        fprintf(stderr, COLOR_MAGENTA "[trace] pid=%d stopped by signal %d" COLOR_RESET "\n", (int)pid, WSTOPSIG(status));
    }
    else
    {
        fprintf(stderr, COLOR_MAGENTA "[trace] pid=%d changed state" COLOR_RESET "\n", (int)pid);
    }
}