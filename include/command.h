/* Command and pipeline data structures shared by the parser and executor. */
#ifndef COMMAND_H
#define COMMAND_H

typedef enum RedirType {
    REDIR_INPUT,
    REDIR_OUTPUT,
    REDIR_APPEND,
    REDIR_ERROR
} RedirType;

typedef struct Redirection {
    RedirType type;
    char* filename;
} Redirection;

typedef struct Command {
    char** argv;
    int argc;

    Redirection* redirs;
    int redir_count;
} Command;

typedef struct Pipeline {
    Command* commands;
    int command_count;
    int background;
} Pipeline;

/* Prepare a Command before adding argv or redirections. */
void command_init_command(Command* cmd);
/* Free all dynamic memory owned by a Command. */
void command_free_command(Command* cmd);

/* Prepare an empty Pipeline. */
void command_init_pipeline(Pipeline* pipeline);
/* Free all commands and reset the Pipeline. */
void command_free_pipeline(Pipeline* pipeline);

/* Add an argument to a command. */
int command_add_arg(Command* cmd, const char* arg);
/* Add a redirection to a command. */
int command_add_redirection(Command* cmd, RedirType type, const char* filename);
/* Append a new command to a pipeline. */
int command_pipeline_add_empty_command(Pipeline* pipeline, Command** out_cmd);

#endif