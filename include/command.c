/* Owns the dynamic command and pipeline containers used after parsing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "command.h"

/* Duplicates command text so parsed data owns its memory. */
static char* command_strdup(const char* s)
{
    if(s == NULL)
    {
        return NULL;
    }

    size_t len = strlen(s);
    char* copy = malloc(len + 1);
    if(copy == NULL)
    {
        perror("procsh: malloc");
        return NULL;
    }

    memcpy(copy, s, len + 1);
    return copy;
}

/* Initializes a Command to an empty, safe state. */
void command_init_command(Command* cmd)
{
    if(cmd == NULL)
    {
        return;
    }

    cmd->argv = NULL;
    cmd->argc = 0;
    cmd->redirs = NULL;
    cmd->redir_count = 0;
}

/* Releases all argv and redirection memory owned by one Command. */
void command_free_command(Command* cmd)
{
    if(cmd == NULL)
    {
        return;
    }

    if(cmd->argv != NULL)
    {
        int i = 0;
        while(cmd->argv[i] != NULL)
        {
            free(cmd->argv[i]);
            i++;
        }

        free(cmd->argv);
    }

    if(cmd->redirs != NULL)
    {
        int i = 0;
        while(i < cmd->redir_count)
        {
            free(cmd->redirs[i].filename);
            i++;
        }

        free(cmd->redirs);
    }

    command_init_command(cmd);
}

/* Initializes a Pipeline before the parser appends commands. */
void command_init_pipeline(Pipeline* pipeline)
{
    if(pipeline == NULL)
    {
        return;
    }

    pipeline->commands = NULL;
    pipeline->command_count = 0;
    pipeline->background = 0;
}

/* Releases every command inside a pipeline and resets the container. */
void command_free_pipeline(Pipeline* pipeline)
{
    if(pipeline == NULL)
    {
        return;
    }

    if(pipeline->commands != NULL)
    {
        int i = 0;
        while(i < pipeline->command_count)
        {
            command_free_command(&pipeline->commands[i]);
            i++;
        }

        free(pipeline->commands);
    }

    command_init_pipeline(pipeline);
}

/* Appends one argv entry and keeps argv NULL-terminated for execvp. */
int command_add_arg(Command* cmd, const char* arg)
{
    if(cmd == NULL || arg == NULL)
    {
        return -1;
    }

    char* copy = command_strdup(arg);
    if(copy == NULL)
    {
        return -1;
    }

    char** new_argv = realloc(cmd->argv, sizeof(char*) * (cmd->argc + 2));
    if(new_argv == NULL)
    {
        perror("procsh: realloc");
        free(copy);
        return -1;
    }

    cmd->argv = new_argv;
    cmd->argv[cmd->argc] = copy;
    cmd->argc++;
    cmd->argv[cmd->argc] = NULL;

    return 0;
}

/* Records one redirection target for later execution setup. */
int command_add_redirection(Command* cmd, RedirType type, const char* filename)
{
    if(cmd == NULL || filename == NULL)
    {
        return -1;
    }

    char* copy = command_strdup(filename);
    if(copy == NULL)
    {
        return -1;
    }

    Redirection* new_redirs = realloc(cmd->redirs, sizeof(Redirection) * (cmd->redir_count + 1));
    if(new_redirs == NULL)
    {
        perror("procsh: realloc");
        free(copy);
        return -1;
    }

    cmd->redirs = new_redirs;
    cmd->redirs[cmd->redir_count].type = type;
    cmd->redirs[cmd->redir_count].filename = copy;
    cmd->redir_count++;

    return 0;
}

/* Grows the pipeline and returns the newly initialized command slot. */
int command_pipeline_add_empty_command(Pipeline* pipeline, Command** out_cmd)
{
    if(pipeline == NULL || out_cmd == NULL)
    {
        return -1;
    }

    Command* new_commands = realloc(
        pipeline->commands,
        sizeof(Command) * (pipeline->command_count + 1)
    );

    if(new_commands == NULL)
    {
        perror("procsh: realloc");
        return -1;
    }

    pipeline->commands = new_commands;

    Command* new_cmd = &pipeline->commands[pipeline->command_count];
    command_init_command(new_cmd);

    pipeline->command_count++;

    *out_cmd = new_cmd;

    return 0;
}