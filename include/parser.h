/* Shell command-line parser interface. */
#ifndef PARSER_H
#define PARSER_H

#include "command.h"

/* Parse one input line into a Pipeline. */
int parser_parse_line(const char* line, Pipeline* pipeline);

#endif