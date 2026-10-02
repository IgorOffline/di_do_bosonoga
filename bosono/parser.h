#ifndef BOSONO_PARSER_H
#define BOSONO_PARSER_H

#include "tokenizer.h"

/* Consumes all token allocations and resets the collection to empty. */
int parse(Tokens* tokens);

#endif
