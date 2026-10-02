#ifndef BOSONO_TOKENIZER_H
#define BOSONO_TOKENIZER_H

#include "bosono.h"

#include <stddef.h>

#define BOSONO_TOKEN_LIMIT (BOSONO_LIMIT / 2)

/* Each token owns its null-terminated text. */
typedef struct Token {
    char* text;
    size_t length;
} Token;

typedef struct Tokens {
    Token* items;
    size_t count;
} Tokens;

/* Consumes a malloc-allocated BOSONO_LIMIT-byte program and clears its pointer.
   Output must be empty; successful output is owned by the caller until parse. */
int tokenize(char** program, Tokens* output);

#endif
