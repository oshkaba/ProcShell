/* Main interactive loop, readline input, prompt formatting, and shell signals. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "shell.h"
#include "command.h"
#include "parser.h"
#include "exec.h"
#include "process.h"
#include "colors.h"

#define RL_COLOR_START "\001"
#define RL_COLOR_END   "\002"

static volatile sig_atomic_t shell_received_sigint = 0;
static volatile sig_atomic_t shell_reading_line = 0;
/* Handles Ctrl-C while the shell is reading a command line. */
static void shell_handle_sigint(int signo)
{
    (void)signo;

    shell_received_sigint = 1;
    write(STDOUT_FILENO, "\n", 1);

    if(shell_reading_line)
    {
        /* readline keeps the current input line in its own buffer. Clear and redraw it so Ctrl-C behaves like a normal interactive shell prompt. */
        rl_replace_line("", 0);
        rl_on_new_line();
        rl_redisplay();
    }
}

/* Installs shell-side signal behavior for interactive use. */
static void shell_install_signal_handlers(void)
{
    struct sigaction action = {0};
    action.sa_handler = shell_handle_sigint;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;

    if(sigaction(SIGINT, &action, NULL) == -1)
    {
        perror("procsh: sigaction");
    }

    /* The shell ignores terminal job-control signals while it owns the terminal. Foreground child groups restore normal signal behavior. */
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
}

/* Makes ProcShell its own process group so it can hand terminal control to jobs. */
static void shell_init_terminal_control(Shell* shell)
{
    if(shell == NULL)
    {
        return;
    }

    shell->terminal_fd = STDIN_FILENO;
    shell->shell_pgid = getpid();

    if(isatty(shell->terminal_fd))
    {
        if(setpgid(shell->shell_pgid, shell->shell_pgid) == -1)
        {
            /* The shell may already be a process-group leader when launched from some terminals, so EPERM is not fatal here. */
        }

        if(tcsetpgrp(shell->terminal_fd, shell->shell_pgid) == -1)
        {
            perror("procsh: tcsetpgrp");
        }
    }
}

/* Shortens home and WSL Windows-user paths in the prompt. */
static void shell_format_prompt_path(const char* cwd, char* display, size_t display_size)
{
    if(cwd == NULL || display == NULL || display_size == 0)
    {
        return;
    }

    const char* home = getenv("HOME");
    if(home != NULL)
    {
        size_t home_len = strlen(home);
        if(strncmp(cwd, home, home_len) == 0 && (cwd[home_len] == '\0' || cwd[home_len] == '/'))
        {
            snprintf(display, display_size, "~%s", cwd + home_len);
            return;
        }
    }

    /* WSL projects often live under /mnt/c/Users/<name>. Treat that folder
       like home too so the prompt stays readable in Windows directories. */
    const char* windows_users = "/mnt/c/Users/";
    if(strncmp(cwd, windows_users, strlen(windows_users)) == 0)
    {
        const char* after_users = cwd + strlen(windows_users);
        const char* after_name = strchr(after_users, '/');

        if(after_name != NULL)
        {
            snprintf(display, display_size, "~%s", after_name);
            return;
        }

        snprintf(display, display_size, "~");
        return;
    }

    snprintf(display, display_size, "%s", cwd);
}

/* Builds the colored readline prompt with invisible escapes marked correctly. */
static void shell_build_prompt(char* prompt, size_t prompt_size)
{
    char cwd[4096];
    char display[4096];

    if(prompt == NULL || prompt_size == 0)
    {
        return;
    }

    if(getcwd(cwd, sizeof(cwd)) == NULL)
    {
        snprintf(display, sizeof(display), "%s", "?");
    }
    else
    {
        shell_format_prompt_path(cwd, display, sizeof(display));
    }

    /* readline needs \001 and \002 around invisible color escape codes so
       cursor movement and command history stay aligned. */
    snprintf(prompt, prompt_size,
        RL_COLOR_START COLOR_BOLD COLOR_CYAN RL_COLOR_END "procsh"
        RL_COLOR_START COLOR_RESET COLOR_GRAY RL_COLOR_END ":"
        RL_COLOR_START COLOR_GREEN RL_COLOR_END "%s"
        RL_COLOR_START COLOR_RESET COLOR_BOLD RL_COLOR_END "$ "
        RL_COLOR_START COLOR_RESET RL_COLOR_END,
        display
    );
}

/* Reads one command line with readline and command history support. */
static char* shell_read_line(void)
{
    char prompt[8192];
    shell_build_prompt(prompt, sizeof(prompt));
    shell_reading_line = 1;
    char* line = readline(prompt);
    shell_reading_line = 0;

    if(shell_received_sigint)
    {
        shell_received_sigint = 0;
    }

    return line;
}

/* Treats empty or whitespace-only input as a no-op. */
static int shell_is_blank_line(const char* line)
{
    if(line == NULL)
    {
        return 1;
    }

    int i = 0;
    while(line[i] != '\0')
    {
        if(line[i] != ' ' && line[i] != '\t')
        {
            return 0;
        }

        i++;
    }

    return 1;
}

/* Initializes the shell state, signal handling, readline, and job table. */
void shell_init(Shell* shell)
{
    if(shell == NULL)
    {
        return;
    }

    shell->running = 1;
    shell->last_status = 0;
    shell->trace_enabled = 0;
    shell->terminal_fd = STDIN_FILENO;
    shell->shell_pgid = getpid();

    rl_catch_signals = 0;
    shell_install_signal_handlers();
    shell_init_terminal_control(shell);

    process_table_init(&shell->process_table);
}

/* Runs the main read-parse-execute loop until exit or EOF. */
void shell_loop(Shell* shell)
{
    if(shell == NULL)
    {
        return;
    }

    while(shell->running)
    {
        process_reap_background_jobs(&shell->process_table);

        char* line = shell_read_line();

        if(line == NULL)
        {
            printf("\n");
            shell->running = 0;
            break;
        }

        if(shell_is_blank_line(line))
        {
            free(line);
            continue;
        }

        /* Store only commands that the user actually entered. */
        add_history(line);

        Pipeline pipeline = {0};
        if(parser_parse_line(line, &pipeline) == 0)
        {
            shell->last_status = exec_run_pipeline(shell, &pipeline);
            command_free_pipeline(&pipeline);
        }
        else
        {
            shell->last_status = 2;
        }

        free(line);
    }
}

/* Releases readline history and marks the shell stopped. */
void shell_destroy(Shell* shell)
{
    if(shell == NULL)
    {
        return;
    }

    clear_history();
    shell->running = 0;
}
