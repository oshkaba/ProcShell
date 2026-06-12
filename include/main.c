/* Program entry point. */
#include "shell.h"

/* Creates the shell, runs it, and performs shutdown cleanup. */
int main(void)
{
    Shell shell;
    shell_init(&shell);
    shell_loop(&shell);
    shell_destroy(&shell);
    return 0;
}