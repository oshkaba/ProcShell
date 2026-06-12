/* Pipeline execution interface. */
#ifndef EXEC_H
#define EXEC_H

#include "shell.h"
#include "command.h"

/* Execute a parsed pipeline and return a shell status code. */
int exec_run_pipeline(Shell* shell, Pipeline* pipeline);

#endif