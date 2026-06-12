/* Converts a command line into tokens and then into an executable pipeline. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "parser.h"
#include "colors.h"

typedef enum TokenType {
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_REDIR_INPUT,
    TOKEN_REDIR_OUTPUT,
    TOKEN_REDIR_APPEND,
    TOKEN_REDIR_ERROR,
    TOKEN_BACKGROUND,
    TOKEN_END
} TokenType;

typedef struct Token {
    TokenType type;
    char* text;
} Token;

typedef struct TokenList {
    Token* tokens;
    int count;
} TokenList;

/* Starts an empty token list for one input line. */
static void token_list_init(TokenList* list)
{
    if(list == NULL)
    {
        return;
    }

    list->tokens = NULL;
    list->count = 0;
}

/* Frees token text and the token array after parsing completes. */
static void token_list_free(TokenList* list)
{
    if(list == NULL)
    {
        return;
    }

    if(list->tokens != NULL)
    {
        int i = 0;
        while(i < list->count)
        {
            free(list->tokens[i].text);
            i++;
        }

        free(list->tokens);
    }

    list->tokens = NULL;
    list->count = 0;
}

/* Copies a substring into a standalone NULL-terminated string. */
static char* parser_strdup_range(const char* start, int len)
{
    char* copy = malloc((size_t)len + 1);
    if(copy == NULL)
    {
        perror("procsh: malloc");
        return NULL;
    }

    memcpy(copy, start, (size_t)len);
    copy[len] = '\0';

    return copy;
}

/* Adds a token and optionally duplicates its text. */
static int token_list_add(TokenList* list, TokenType type, const char* text)
{
    if(list == NULL)
    {
        return -1;
    }

    char* copy = NULL;

    if(text != NULL)
    {
        copy = parser_strdup_range(text, (int)strlen(text));
        if(copy == NULL)
        {
            return -1;
        }
    }

    Token* new_tokens = realloc(list->tokens, sizeof(Token) * (list->count + 1));
    if(new_tokens == NULL)
    {
        perror("procsh: realloc");
        free(copy);
        return -1;
    }

    list->tokens = new_tokens;
    list->tokens[list->count].type = type;
    list->tokens[list->count].text = copy;
    list->count++;

    return 0;
}

/* Adds a token that already owns its allocated text. */
static int token_list_add_owned(TokenList* list, TokenType type, char* text)
{
    if(list == NULL)
    {
        free(text);
        return -1;
    }

    Token* new_tokens = realloc(list->tokens, sizeof(Token) * (list->count + 1));
    if(new_tokens == NULL)
    {
        perror("procsh: realloc");
        free(text);
        return -1;
    }

    list->tokens = new_tokens;
    list->tokens[list->count].type = type;
    list->tokens[list->count].text = text;
    list->count++;

    return 0;
}

/* Identifies parser metacharacters that end unquoted words. */
static int is_special_char(char c)
{
    return c == '|' || c == '<' || c == '>' || c == '&';
}

/* Appends one character while growing the word buffer as needed. */
static int word_append_char(char** buffer, int* len, int* capacity, char c)
{
    if(*len + 1 >= *capacity)
    {
        *capacity = *capacity * 2;

        char* new_buffer = realloc(*buffer, (size_t)*capacity);
        if(new_buffer == NULL)
        {
            perror("procsh: realloc");
            free(*buffer);
            *buffer = NULL;
            return -1;
        }

        *buffer = new_buffer;
    }

    (*buffer)[*len] = c;
    (*len)++;
    (*buffer)[*len] = '\0';

    return 0;
}

/* Reads one shell word, honoring quotes and escaped characters. */
static int read_word(const char* line, int* index, char** out_word)
{
    int capacity = 32;
    int len = 0;
    char quote = '\0';

    char* buffer = malloc((size_t)capacity);
    if(buffer == NULL)
    {
        perror("procsh: malloc");
        return -1;
    }

    buffer[0] = '\0';

    while(line[*index] != '\0')
    {
        if(quote == '\0')
        {
            if(isspace((unsigned char)line[*index]) || is_special_char(line[*index]))
            {
                break;
            }

            if(line[*index] == '\'' || line[*index] == '"')
            {
                quote = line[*index];
                (*index)++;
                continue;
            }

            if(line[*index] == '\\' && line[*index + 1] != '\0')
            {
                (*index)++;

                if(word_append_char(&buffer, &len, &capacity, line[*index]) == -1)
                {
                    return -1;
                }

                (*index)++;
                continue;
            }

            if(word_append_char(&buffer, &len, &capacity, line[*index]) == -1)
            {
                return -1;
            }

            (*index)++;
            continue;
        }

        if(line[*index] == quote)
        {
            quote = '\0';
            (*index)++;
            continue;
        }

        if(quote == '"' && line[*index] == '\\' && line[*index + 1] != '\0')
        {
            /* Preserve ordinary backslash sequences inside double quotes.
               Only remove the backslash for characters that shell quoting treats
               as escaped, so printf "one\\n" still receives \\n. */
            if(line[*index + 1] == '"' || line[*index + 1] == '\\' ||
                line[*index + 1] == '$' || line[*index + 1] == '`')
            {
                (*index)++;

                if(word_append_char(&buffer, &len, &capacity, line[*index]) == -1)
                {
                    return -1;
                }

                (*index)++;
                continue;
            }
        }

        if(word_append_char(&buffer, &len, &capacity, line[*index]) == -1)
        {
            return -1;
        }

        (*index)++;
    }

    if(quote != '\0')
    {
        fprintf(stderr, "procsh: syntax error: unmatched quote\n");
        free(buffer);
        return -1;
    }

    *out_word = buffer;
    return 0;
}

/* Splits the raw line into words, pipes, redirects, background markers, and end. */
static int tokenize_line(const char* line, TokenList* tokens)
{
    if(line == NULL || tokens == NULL)
    {
        return -1;
    }

    token_list_init(tokens);

    int i = 0;
    while(line[i] != '\0')
    {
        while(isspace((unsigned char)line[i]))
        {
            i++;
        }

        if(line[i] == '\0')
        {
            break;
        }

        /* Operators become standalone tokens so parsing can validate placement. */
        if(line[i] == '|')
        {
            if(token_list_add(tokens, TOKEN_PIPE, NULL) == -1)
            {
                return -1;
            }

            i++;
            continue;
        }

        if(line[i] == '<')
        {
            if(token_list_add(tokens, TOKEN_REDIR_INPUT, NULL) == -1)
            {
                return -1;
            }

            i++;
            continue;
        }

        if(line[i] == '>')
        {
            if(line[i + 1] == '>')
            {
                if(token_list_add(tokens, TOKEN_REDIR_APPEND, NULL) == -1)
                {
                    return -1;
                }

                i += 2;
            }
            else
            {
                if(token_list_add(tokens, TOKEN_REDIR_OUTPUT, NULL) == -1)
                {
                    return -1;
                }

                i++;
            }

            continue;
        }

        if(line[i] == '2' && line[i + 1] == '>')
        {
            if(token_list_add(tokens, TOKEN_REDIR_ERROR, NULL) == -1)
            {
                return -1;
            }

            i += 2;
            continue;
        }

        if(line[i] == '&')
        {
            if(token_list_add(tokens, TOKEN_BACKGROUND, NULL) == -1)
            {
                return -1;
            }

            i++;
            continue;
        }

        char* word = NULL;
        if(read_word(line, &i, &word) == -1)
        {
            return -1;
        }

        if(word[0] == '\0')
        {
            free(word);
            continue;
        }

        if(token_list_add_owned(tokens, TOKEN_WORD, word) == -1)
        {
            return -1;
        }
    }

    if(token_list_add(tokens, TOKEN_END, NULL) == -1)
    {
        return -1;
    }

    return 0;
}

/* Returns whether a token represents any supported redirection operator. */
static int token_is_redirection(TokenType type)
{
    return type == TOKEN_REDIR_INPUT || type == TOKEN_REDIR_OUTPUT || type == TOKEN_REDIR_APPEND || type == TOKEN_REDIR_ERROR;
}

/* Converts a parser token into the command redirection enum. */
static RedirType token_to_redir_type(TokenType type)
{
    if(type == TOKEN_REDIR_INPUT)
    {
        return REDIR_INPUT;
    }

    if(type == TOKEN_REDIR_OUTPUT)
    {
        return REDIR_OUTPUT;
    }

    if(type == TOKEN_REDIR_APPEND)
    {
        return REDIR_APPEND;
    }

    return REDIR_ERROR;
}

/* Gives readable operator names for syntax error messages. */
static const char* token_name(TokenType type)
{
    if(type == TOKEN_PIPE)
    {
        return "|";
    }

    if(type == TOKEN_REDIR_INPUT)
    {
        return "<";
    }

    if(type == TOKEN_REDIR_OUTPUT)
    {
        return ">";
    }

    if(type == TOKEN_REDIR_APPEND)
    {
        return ">>";
    }

    if(type == TOKEN_REDIR_ERROR)
    {
        return "2>";
    }

    if(type == TOKEN_BACKGROUND)
    {
        return "&";
    }

    return "end of line";
}

/* Builds a Pipeline from tokens while validating shell grammar. */
static int parse_tokens(TokenList* tokens, Pipeline* pipeline)
{
    if(tokens == NULL || pipeline == NULL)
    {
        return -1;
    }

    command_init_pipeline(pipeline);

    Command* current = NULL;
    int i = 0;
    while(i < tokens->count)
    {
        Token* tok = &tokens->tokens[i];

        if(tok->type == TOKEN_END)
        {
            break;
        }

        if(tok->type == TOKEN_WORD)
        {
            if(current == NULL)
            {
                if(command_pipeline_add_empty_command(pipeline, &current) == -1)
                {
                    command_free_pipeline(pipeline);
                    return -1;
                }
            }

            if(command_add_arg(current, tok->text) == -1)
            {
                command_free_pipeline(pipeline);
                return -1;
            }

            i++;
            continue;
        }

        if(token_is_redirection(tok->type))
        {
            if(current == NULL)
            {
                if(command_pipeline_add_empty_command(pipeline, &current) == -1)
                {
                    command_free_pipeline(pipeline);
                    return -1;
                }
            }

            /* Redirections must be followed by a filename token. */
            if(tokens->tokens[i + 1].type != TOKEN_WORD)
            {
                fprintf(stderr, "procsh: syntax error: expected filename after '%s'\n", token_name(tok->type));
                command_free_pipeline(pipeline);
                return -1;
            }

            if(command_add_redirection(current, token_to_redir_type(tok->type), tokens->tokens[i + 1].text) == -1)
            {
                command_free_pipeline(pipeline);
                return -1;
            }

            i += 2;
            continue;
        }

        if(tok->type == TOKEN_PIPE)
        {
            if(current == NULL || current->argc == 0)
            {
                fprintf(stderr, "procsh: syntax error near unexpected token '|'\n");
                command_free_pipeline(pipeline);
                return -1;
            }

            if(tokens->tokens[i + 1].type == TOKEN_END)
            {
                fprintf(stderr, "procsh: syntax error: missing command after pipe\n");
                command_free_pipeline(pipeline);
                return -1;
            }

            if(tokens->tokens[i + 1].type == TOKEN_PIPE)
            {
                fprintf(stderr, "procsh: syntax error near unexpected token '|'\n");
                command_free_pipeline(pipeline);
                return -1;
            }

            current = NULL;
            i++;
            continue;
        }

        if(tok->type == TOKEN_BACKGROUND)
        {
            if(current == NULL || current->argc == 0)
            {
                fprintf(stderr, "procsh: syntax error near unexpected token '&'\n");
                command_free_pipeline(pipeline);
                return -1;
            }

            if(tokens->tokens[i + 1].type != TOKEN_END)
            {
                fprintf(stderr, "procsh: syntax error: '&' must appear at the end\n");
                command_free_pipeline(pipeline);
                return -1;
            }

            pipeline->background = 1;
            i++;
            continue;
        }

        fprintf(stderr, "procsh: syntax error near unexpected token '%s'\n", token_name(tok->type));

        command_free_pipeline(pipeline);
        return -1;
    }

    if(pipeline->command_count == 0)
    {
        fprintf(stderr, "procsh: syntax error: empty command\n");
        command_free_pipeline(pipeline);
        return -1;
    }

    if(pipeline->commands[pipeline->command_count - 1].argc == 0)
    {
        fprintf(stderr, "procsh: syntax error: missing command\n");
        command_free_pipeline(pipeline);
        return -1;
    }

    return 0;
}

/* Public parse entry point: tokenize, parse, then free temporary tokens. */
int parser_parse_line(const char* line, Pipeline* pipeline)
{
    if(line == NULL || pipeline == NULL)
    {
        return -1;
    }

    TokenList tokens = {0};
    token_list_init(&tokens);

    if(tokenize_line(line, &tokens) == -1)
    {
        token_list_free(&tokens);
        return -1;
    }

    int result = parse_tokens(&tokens, pipeline);

    token_list_free(&tokens);

    return result;
}