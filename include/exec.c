/* Executes built-ins, external commands, pipelines, redirection, and jobs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "exec.h"
#include "process.h"
#include "trace.h"
#include "procfs.h"
#include "colors.h"

static int exec_wait_for_process_group(Shell* shell, pid_t pgid, pid_t* pids, int count, int* stopped);
static int exec_continue_job_foreground(Shell* shell, Job* job);
static int exec_continue_job_background(Job* job);

/* Restores external-command signal behavior before running execvp. */
static void exec_prepare_child_signals(void)
{
    /* The shell protects itself from terminal signals, but external commands must receive normal terminal behavior for Ctrl-C and Ctrl-Z. */
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
}

/* Checks whether a command name is handled inside ProcShell. */
static int exec_is_builtin(Command* cmd)
{
    if(cmd == NULL || cmd->argc == 0 || cmd->argv == NULL)
    {
        return 0;
    }

    return strcmp(cmd->argv[0], "cd") == 0 || strcmp(cmd->argv[0], "pwd") == 0 || strcmp(cmd->argv[0], "exit") == 0 ||
           strcmp(cmd->argv[0], "help") == 0 || strcmp(cmd->argv[0], "trace") == 0 || strcmp(cmd->argv[0], "jobs") == 0 ||
           strcmp(cmd->argv[0], "fg") == 0 || strcmp(cmd->argv[0], "bg") == 0 || strcmp(cmd->argv[0], "psx") == 0;
}


/* Adds ls coloring only when the user did not already choose a color mode. */
static int exec_should_color_ls(Command* cmd)
{
    if(cmd == NULL || cmd->argc <= 0 || cmd->argv == NULL || cmd->argv[0] == NULL)
    {
        return 0;
    }

    if(strcmp(cmd->argv[0], "ls") != 0)
    {
        return 0;
    }

    int i = 1;
    while(i < cmd->argc)
    {
        if(strncmp(cmd->argv[i], "--color", 7) == 0)
        {
            return 0;
        }

        i++;
    }

    return 1;
}

/* Inserts --color=auto into ls argv so directories are easier to see. */
static void exec_apply_ls_color(Command* cmd)
{
    if(!exec_should_color_ls(cmd))
    {
        return;
    }

    char** colored_argv = malloc(sizeof(char*) * (cmd->argc + 2));
    if(colored_argv == NULL)
    {
        return;
    }

    colored_argv[0] = cmd->argv[0];
    colored_argv[1] = "--color=auto";

    int i = 1;
    while(i < cmd->argc)
    {
        colored_argv[i + 1] = cmd->argv[i];
        i++;
    }

    colored_argv[cmd->argc + 1] = NULL;
    cmd->argv = colored_argv;
    cmd->argc++;
}

/* Implements cd with HOME as the default target. */
static int exec_builtin_cd(Command* cmd)
{
    const char* path = NULL;

    if(cmd->argc > 2)
    {
        fprintf(stderr, COLOR_RED "procsh: cd: too many arguments" COLOR_RESET "\n");
        return 1;
    }

    if(cmd->argc == 1)
    {
        path = getenv("HOME");

        if(path == NULL)
        {
            fprintf(stderr, COLOR_RED "procsh: cd: HOME not set" COLOR_RESET "\n");
            return 1;
        }
    }
    else
    {
        path = cmd->argv[1];
    }

    if(chdir(path) == -1)
    {
        perror("procsh: cd");
        return 1;
    }

    return 0;
}

/* Implements pwd using getcwd. */
static int exec_builtin_pwd(void)
{
    char cwd[4096];

    if(getcwd(cwd, sizeof(cwd)) == NULL)
    {
        perror("procsh: getcwd");
        return 1;
    }

    printf("%s\n", cwd);
    return 0;
}

/* Prints the built-in command reference. */
static int exec_builtin_help(void)
{
    printf(COLOR_BOLD COLOR_CYAN "procsh built-ins" COLOR_RESET "\n");
    printf("  cd [dir]        change directory\n");
    printf("  pwd             print current directory\n");
    printf("  exit            exit the shell\n");
    printf("  help            show this help message\n");
    printf("  trace on        enable trace mode\n");
    printf("  trace off       disable trace mode\n");
    printf("  trace status    show trace mode status\n");
    printf("  jobs            list background and stopped jobs\n");
    printf("  fg %%job         resume a stopped/background job in the foreground\n");
    printf("  bg %%job         resume a stopped job in the background\n");
    printf("  psx [pid]       show process information from /proc\n");
    printf("\n");
    printf(COLOR_BOLD COLOR_CYAN "keyboard shortcuts" COLOR_RESET "\n");
    printf("  Ctrl-C          stop the current foreground command\n");
    printf("  Ctrl-Z          stop the current foreground command and save it as a job\n");
    printf("  Up/Down         cycle through command history\n");

    return 0;
}

/* Implements trace on/off/status. */
static int exec_builtin_trace(Shell* shell, Command* cmd)
{
    if(shell == NULL || cmd == NULL)
    {
        return 1;
    }

    if(cmd->argc != 2)
    {
        fprintf(stderr, COLOR_RED "procsh: trace: expected 'on', 'off', or 'status'" COLOR_RESET "\n");
        return 1;
    }

    if(strcmp(cmd->argv[1], "on") == 0)
    {
        trace_set_enabled(shell, 1);
        printf(COLOR_GREEN "trace enabled" COLOR_RESET "\n");
        return 0;
    }

    if(strcmp(cmd->argv[1], "off") == 0)
    {
        trace_set_enabled(shell, 0);
        printf(COLOR_YELLOW "trace disabled" COLOR_RESET "\n");
        return 0;
    }

    if(strcmp(cmd->argv[1], "status") == 0)
    {
        trace_print_status(shell);
        return 0;
    }

    fprintf(stderr, COLOR_RED "procsh: trace: expected 'on', 'off', or 'status'" COLOR_RESET "\n");
    return 1;
}

/* Implements psx with optional PID validation. */
static int exec_builtin_psx(Command* cmd)
{
    if(cmd == NULL)
    {
        return 1;
    }

    if(cmd->argc == 1)
    {
        return procfs_print_all();
    }

    if(cmd->argc != 2)
    {
        fprintf(stderr, COLOR_RED "procsh: psx: usage: psx [pid]" COLOR_RESET "\n");
        return 1;
    }

    char* endptr = NULL;
    pid_t pid = (pid_t)strtol(cmd->argv[1], &endptr, 10);

    if(endptr == cmd->argv[1] || *endptr != '\0' || pid <= 0)
    {
        fprintf(stderr, COLOR_RED "procsh: psx: invalid pid" COLOR_RESET "\n");
        return 1;
    }

    return procfs_print_pid(pid);
}

/* Parses job arguments such as %1 or 1 for fg/bg. */
static int exec_parse_job_id(const char* text, int* job_id)
{
    if(text == NULL || job_id == NULL)
    {
        return -1;
    }

    const char* start = text;
    if(start[0] == '%')
    {
        start++;
    }

    if(start[0] == '\0')
    {
        return -1;
    }

    char* endptr = NULL;
    long value = strtol(start, &endptr, 10);

    if(endptr == start || *endptr != '\0' || value <= 0)
    {
        return -1;
    }

    *job_id = (int)value;
    return 0;
}

/* Resumes a stopped or background job in the foreground. */
static int exec_builtin_fg(Shell* shell, Command* cmd)
{
    if(shell == NULL || cmd == NULL || cmd->argc != 2)
    {
        fprintf(stderr, COLOR_RED "procsh: fg: usage: fg %%job" COLOR_RESET "\n");
        return 1;
    }

    int job_id = 0;
    if(exec_parse_job_id(cmd->argv[1], &job_id) == -1)
    {
        fprintf(stderr, COLOR_RED "procsh: fg: invalid job" COLOR_RESET "\n");
        return 1;
    }

    Job* job = process_find_job(&shell->process_table, job_id);
    if(job == NULL)
    {
        fprintf(stderr, COLOR_RED "procsh: fg: job not found" COLOR_RESET "\n");
        return 1;
    }

    return exec_continue_job_foreground(shell, job);
}

/* Resumes a stopped job in the background. */
static int exec_builtin_bg(Shell* shell, Command* cmd)
{
    if(shell == NULL || cmd == NULL || cmd->argc != 2)
    {
        fprintf(stderr, COLOR_RED "procsh: bg: usage: bg %%job" COLOR_RESET "\n");
        return 1;
    }

    int job_id = 0;
    if(exec_parse_job_id(cmd->argv[1], &job_id) == -1)
    {
        fprintf(stderr, COLOR_RED "procsh: bg: invalid job" COLOR_RESET "\n");
        return 1;
    }

    Job* job = process_find_job(&shell->process_table, job_id);
    if(job == NULL)
    {
        fprintf(stderr, COLOR_RED "procsh: bg: job not found" COLOR_RESET "\n");
        return 1;
    }

    return exec_continue_job_background(job);
}

/* Dispatches one built-in command to its handler. */
static int exec_run_builtin(Shell* shell, Command* cmd)
{
    if(shell == NULL || cmd == NULL || cmd->argc == 0)
    {
        return 1;
    }

    if(strcmp(cmd->argv[0], "cd") == 0)
    {
        return exec_builtin_cd(cmd);
    }

    if(strcmp(cmd->argv[0], "pwd") == 0)
    {
        return exec_builtin_pwd();
    }

    if(strcmp(cmd->argv[0], "exit") == 0)
    {
        if(cmd->argc > 1)
        {
            fprintf(stderr, COLOR_RED "procsh: exit: too many arguments" COLOR_RESET "\n");
            return 1;
        }

        shell->running = 0;
        return 0;
    }

    if(strcmp(cmd->argv[0], "help") == 0)
    {
        return exec_builtin_help();
    }

    if(strcmp(cmd->argv[0], "trace") == 0)
    {
        return exec_builtin_trace(shell, cmd);
    }

    if(strcmp(cmd->argv[0], "jobs") == 0)
    {
        if(cmd->argc != 1)
        {
            fprintf(stderr, COLOR_RED "procsh: jobs: too many arguments" COLOR_RESET "\n");
            return 1;
        }

        process_print_jobs(&shell->process_table);
        return 0;
    }

    if(strcmp(cmd->argv[0], "fg") == 0)
    {
        return exec_builtin_fg(shell, cmd);
    }

    if(strcmp(cmd->argv[0], "bg") == 0)
    {
        return exec_builtin_bg(shell, cmd);
    }

    if(strcmp(cmd->argv[0], "psx") == 0)
    {
        return exec_builtin_psx(cmd);
    }

    return 1;
}

/* Opens the correct file descriptor for <, >, >>, or 2>. */
static int exec_open_redirection_file(Redirection* redir)
{
    if(redir == NULL || redir->filename == NULL)
    {
        return -1;
    }

    if(redir->type == REDIR_INPUT)
    {
        int fd = open(redir->filename, O_RDONLY);
        if(fd == -1)
        {
            perror("procsh: open");
        }

        return fd;
    }

    if(redir->type == REDIR_OUTPUT)
    {
        int fd = open(redir->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(fd == -1)
        {
            perror("procsh: open");
        }

        return fd;
    }

    if(redir->type == REDIR_APPEND)
    {
        int fd = open(redir->filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if(fd == -1)
        {
            perror("procsh: open");
        }

        return fd;
    }

    if(redir->type == REDIR_ERROR)
    {
        int fd = open(redir->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(fd == -1)
        {
            perror("procsh: open");
        }

        return fd;
    }

    return -1;
}

/* Maps a redirection type to stdin, stdout, or stderr. */
static int exec_redirection_target_fd(Redirection* redir)
{
    if(redir->type == REDIR_INPUT)
    {
        return STDIN_FILENO;
    }

    if(redir->type == REDIR_OUTPUT || redir->type == REDIR_APPEND)
    {
        return STDOUT_FILENO;
    }

    if(redir->type == REDIR_ERROR)
    {
        return STDERR_FILENO;
    }

    return -1;
}

/* Applies all redirections for a command using dup2. */
static int exec_setup_redirections(Shell* shell, Command* cmd)
{
    if(cmd == NULL)
    {
        return -1;
    }

    int i = 0;
    while(i < cmd->redir_count)
    {
        int fd = exec_open_redirection_file(&cmd->redirs[i]);
        if(fd == -1)
        {
            return -1;
        }

        int target_fd = exec_redirection_target_fd(&cmd->redirs[i]);
        if(target_fd == -1)
        {
            close(fd);
            fprintf(stderr, COLOR_RED "procsh: internal error: bad redirection target" COLOR_RESET "\n");
            return -1;
        }

        trace_redirection(shell, cmd->redirs[i].filename, target_fd);
        trace_dup2(shell, fd, target_fd, "redirection");

        if(dup2(fd, target_fd) == -1)
        {
            perror("procsh: dup2");

            if(close(fd) == -1)
            {
                perror("procsh: close");
            }

            return -1;
        }

        if(close(fd) == -1)
        {
            perror("procsh: close");
            return -1;
        }

        i++;
    }

    return 0;
}

/* Saves stdin, stdout, and stderr before running redirected built-ins. */
static int exec_save_standard_fds(int saved_fds[3])
{
    saved_fds[0] = dup(STDIN_FILENO);
    if(saved_fds[0] == -1)
    {
        perror("procsh: dup");
        return -1;
    }

    saved_fds[1] = dup(STDOUT_FILENO);
    if(saved_fds[1] == -1)
    {
        perror("procsh: dup");
        close(saved_fds[0]);
        return -1;
    }

    saved_fds[2] = dup(STDERR_FILENO);
    if(saved_fds[2] == -1)
    {
        perror("procsh: dup");
        close(saved_fds[0]);
        close(saved_fds[1]);
        return -1;
    }

    return 0;
}

/* Restores the shell's standard descriptors after a redirected built-in. */
static int exec_restore_standard_fds(int saved_fds[3])
{
    int result = 0;

    if(dup2(saved_fds[0], STDIN_FILENO) == -1)
    {
        perror("procsh: dup2");
        result = -1;
    }

    if(dup2(saved_fds[1], STDOUT_FILENO) == -1)
    {
        perror("procsh: dup2");
        result = -1;
    }

    if(dup2(saved_fds[2], STDERR_FILENO) == -1)
    {
        perror("procsh: dup2");
        result = -1;
    }

    if(close(saved_fds[0]) == -1)
    {
        perror("procsh: close");
        result = -1;
    }

    if(close(saved_fds[1]) == -1)
    {
        perror("procsh: close");
        result = -1;
    }

    if(close(saved_fds[2]) == -1)
    {
        perror("procsh: close");
        result = -1;
    }

    return result;
}

/* Runs a parent-shell built-in while temporarily applying redirection. */
static int exec_run_builtin_with_redirection(Shell* shell, Command* cmd)
{
    int saved_fds[3];

    if(exec_save_standard_fds(saved_fds) == -1)
    {
        return 1;
    }

    if(exec_setup_redirections(shell, cmd) == -1)
    {
        exec_restore_standard_fds(saved_fds);
        return 1;
    }

    int status = exec_run_builtin(shell, cmd);

    if(exec_restore_standard_fds(saved_fds) == -1)
    {
        return 1;
    }

    return status;
}

/* Closes every pipe descriptor after children have inherited what they need. */
static void exec_close_all_pipes(int* pipes, int pipe_count)
{
    if(pipes == NULL)
    {
        return;
    }

    int i = 0;
    while(i < pipe_count * 2)
    {
        if(close(pipes[i]) == -1)
        {
            perror("procsh: close");
        }

        i++;
    }
}

/* Creates the pipe array required for a multi-command pipeline. */
static int exec_create_pipes(Shell* shell, int* pipes, int pipe_count)
{
    int i = 0;
    while(i < pipe_count)
    {
        if(pipe(&pipes[i * 2]) == -1)
        {
            perror("procsh: pipe");

            while(i > 0)
            {
                i--;

                if(close(pipes[i * 2]) == -1)
                {
                    perror("procsh: close");
                }

                if(close(pipes[i * 2 + 1]) == -1)
                {
                    perror("procsh: close");
                }
            }

            return -1;
        }

        trace_pipe_created(shell, pipes[i * 2], pipes[i * 2 + 1]);

        i++;
    }

    return 0;
}

/* Connects one child process to its pipeline input and output. */
static int exec_setup_pipeline_fds(Shell* shell, int* pipes, int pipe_count, int index, int command_count)
{
    if(index > 0)
    {
        int read_fd = pipes[(index - 1) * 2];

        trace_dup2(shell, read_fd, STDIN_FILENO, "pipeline input");

        if(dup2(read_fd, STDIN_FILENO) == -1)
        {
            perror("procsh: dup2");
            return -1;
        }
    }

    if(index < command_count - 1)
    {
        int write_fd = pipes[index * 2 + 1];

        trace_dup2(shell, write_fd, STDOUT_FILENO, "pipeline output");

        if(dup2(write_fd, STDOUT_FILENO) == -1)
        {
            perror("procsh: dup2");
            return -1;
        }
    }

    exec_close_all_pipes(pipes, pipe_count);

    return 0;
}

/* Child-side execution path after fork. It sets up fds, then runs built-in or execvp. */
static void exec_child_process(Shell* shell, Command* cmd, int* pipes, int pipe_count, int index, int command_count, pid_t pgid, int use_process_group)
{
    exec_prepare_child_signals();

    if(use_process_group)
    {
        if(pgid == 0)
        {
            if(setpgid(0, 0) == -1)
            {
                perror("procsh: setpgid");
            }
        }
        else
        {
            if(setpgid(0, pgid) == -1)
            {
                perror("procsh: setpgid");
            }
        }
    }

    if(exec_setup_pipeline_fds(shell, pipes, pipe_count, index, command_count) == -1)
    {
        _exit(1);
    }

    if(exec_setup_redirections(shell, cmd) == -1)
    {
        _exit(1);
    }

    if(exec_is_builtin(cmd))
    {
        int builtin_status = exec_run_builtin(shell, cmd);
        _exit(builtin_status);
    }

    exec_apply_ls_color(cmd);

    trace_exec(shell, cmd);

    execvp(cmd->argv[0], cmd->argv);

    perror("procsh: execvp");
    _exit(127);
}

/* Waits for a foreground process group until it exits or stops. */
static int exec_wait_for_process_group(Shell* shell, pid_t pgid, pid_t* pids, int count, int* stopped)
{
    int last_status = 0;
    int remaining = count;

    if(stopped != NULL)
    {
        *stopped = 0;
    }

    while(remaining > 0)
    {
        int status = 0;
        pid_t result = waitpid(-pgid, &status, WUNTRACED);

        if(result == -1)
        {
            perror("procsh: waitpid");
            return 1;
        }

        trace_child_exit(shell, result, status);

        if(WIFSTOPPED(status))
        {
            if(stopped != NULL)
            {
                *stopped = 1;
            }

            return 128 + WSTOPSIG(status);
        }

        if(WIFEXITED(status))
        {
            last_status = WEXITSTATUS(status);
            remaining--;
        }
        else if(WIFSIGNALED(status))
        {
            last_status = 128 + WTERMSIG(status);
            remaining--;
        }
    }

    return last_status;
}

/* Ctrl-C is echoed by the terminal as ^C without a newline while the foreground job owns the terminal. Print the newline before the next prompt. */
static void exec_print_interrupt_newline(int status)
{
    if(status == 128 + SIGINT)
    {
        printf("\n");
        fflush(stdout);
    }
}

/* Gives a job the terminal and waits for it as the foreground job. */
static int exec_continue_job_foreground(Shell* shell, Job* job)
{
    if(shell == NULL || job == NULL)
    {
        return 1;
    }

    if(kill(-job->pgid, SIGCONT) == -1)
    {
        perror("procsh: kill");
        return 1;
    }

    job->status = JOB_RUNNING;

    if(isatty(shell->terminal_fd))
    {
        if(tcsetpgrp(shell->terminal_fd, job->pgid) == -1)
        {
            perror("procsh: tcsetpgrp");
        }
    }

    int stopped = 0;
    int status = exec_wait_for_process_group(shell, job->pgid, job->pids, job->active_processes, &stopped);

    if(isatty(shell->terminal_fd))
    {
        if(tcsetpgrp(shell->terminal_fd, shell->shell_pgid) == -1)
        {
            perror("procsh: tcsetpgrp");
        }
    }

    exec_print_interrupt_newline(status);

    if(stopped)
    {
        job->status = JOB_STOPPED;
        printf(COLOR_YELLOW "[%d] stopped  %d  %s" COLOR_RESET "\n", job->id, (int)job->pgid, job->command);
        return status;
    }

    process_remove_job(&shell->process_table, job->id);
    return status;
}

/* Sends SIGCONT to a job while leaving it in the background. */
static int exec_continue_job_background(Job* job)
{
    if(job == NULL)
    {
        return 1;
    }

    if(kill(-job->pgid, SIGCONT) == -1)
    {
        perror("procsh: kill");
        return 1;
    }

    job->status = JOB_RUNNING;
    printf(COLOR_YELLOW "[%d] running  %d  %s" COLOR_RESET "\n", job->id, (int)job->pgid, job->command);

    return 0;
}

/* Reconstructs a compact job command string for the jobs table. */
static void exec_build_command_text(Pipeline* pipeline, char* buffer, int size)
{
    if(buffer == NULL || size <= 0)
    {
        return;
    }

    buffer[0] = '\0';

    int used = 0;
    int i = 0;
    while(i < pipeline->command_count)
    {
        if(i > 0)
        {
            used += snprintf(buffer + used, (size_t)(size - used), " | ");
        }

        int j = 0;
        while(j < pipeline->commands[i].argc)
        {
            if(j > 0)
            {
                used += snprintf(buffer + used, (size_t)(size - used), " ");
            }

            used += snprintf(
                buffer + used,
                (size_t)(size - used),
                "%s",
                pipeline->commands[i].argv[j]
            );

            if(used >= size)
            {
                buffer[size - 1] = '\0';
                return;
            }

            j++;
        }

        i++;
    }

    /* Background jobs are recorded instead of being waited on here. */
    if(pipeline->background)
    {
        snprintf(buffer + used, (size_t)(size - used), " &");
    }
}

/* Forks and runs a full pipeline, either foreground or background. */
static int exec_run_external_pipeline(Shell* shell, Pipeline* pipeline)
{
    int command_count = pipeline->command_count;
    int pipe_count = command_count - 1;
    int* pipes = NULL;
    pid_t* pids = NULL;
    pid_t pgid = 0;
    int use_process_group = 1;
    char command_text[PROCESS_MAX_COMMAND_TEXT];

    if(pipe_count > 0)
    {
        pipes = malloc(sizeof(int) * pipe_count * 2);
        if(pipes == NULL)
        {
            perror("procsh: malloc");
            return 1;
        }

        if(exec_create_pipes(shell, pipes, pipe_count) == -1)
        {
            free(pipes);
            return 1;
        }
    }

    pids = malloc(sizeof(pid_t) * command_count);
    if(pids == NULL)
    {
        perror("procsh: malloc");

        if(pipes != NULL)
        {
            exec_close_all_pipes(pipes, pipe_count);
            free(pipes);
        }

        return 1;
    }

    int i = 0;
    while(i < command_count)
    {
        pids[i] = fork();

        if(pids[i] == -1)
        {
            perror("procsh: fork");

            exec_close_all_pipes(pipes, pipe_count);

            free(pipes);
            free(pids);

            return 1;
        }

        if(pids[i] == 0)
        {
            exec_child_process(shell, &pipeline->commands[i], pipes, pipe_count, i, command_count, pgid, use_process_group);
        }

        if(i == 0)
        {
            pgid = pids[i];
        }

        if(use_process_group)
        {
            if(setpgid(pids[i], pgid) == -1)
            {
                perror("procsh: setpgid");
            }
        }

        trace_fork(shell, pids[i], &pipeline->commands[i]);

        i++;
    }

    exec_close_all_pipes(pipes, pipe_count);

    if(pipeline->background)
    {
        exec_build_command_text(pipeline, command_text, PROCESS_MAX_COMMAND_TEXT);
        process_add_job(&shell->process_table, pgid, pids, command_count, command_text, JOB_RUNNING);

        free(pipes);
        free(pids);

        return 0;
    }

    if(isatty(shell->terminal_fd))
    {
        if(tcsetpgrp(shell->terminal_fd, pgid) == -1)
        {
            perror("procsh: tcsetpgrp");
        }
    }

    int stopped = 0;
    int status = exec_wait_for_process_group(shell, pgid, pids, command_count, &stopped);

    if(isatty(shell->terminal_fd))
    {
        if(tcsetpgrp(shell->terminal_fd, shell->shell_pgid) == -1)
        {
            perror("procsh: tcsetpgrp");
        }
    }

    exec_print_interrupt_newline(status);

    if(stopped)
    {
        exec_build_command_text(pipeline, command_text, PROCESS_MAX_COMMAND_TEXT);
        process_add_job(&shell->process_table, pgid, pids, command_count, command_text, JOB_STOPPED);
    }

    free(pipes);
    free(pids);

    return status;
}

/* Executes one parsed pipeline, using the parent only for simple foreground built-ins. */
int exec_run_pipeline(Shell* shell, Pipeline* pipeline)
{
    if(shell == NULL || pipeline == NULL)
    {
        return 1;
    }

    if(pipeline->command_count <= 0)
    {
        return 0;
    }

    Command* cmd = &pipeline->commands[0];

    /* Simple foreground built-ins run in the parent so cd and exit can affect the shell. */
    if(pipeline->command_count == 1 &&
        pipeline->background == 0 &&
        exec_is_builtin(cmd))
    {
        return exec_run_builtin_with_redirection(shell, cmd);
    }

    return exec_run_external_pipeline(shell, pipeline);
}