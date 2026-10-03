#ifndef BOSONO_TOKENIZER_H
#define BOSONO_TOKENIZER_H

#include <stddef.h>

#include "bosono.h"

#define BOSONO_TOKEN_LIMIT (BOSONO_LIMIT / 2)

typedef struct Token {
  char* text;
  size_t length;
} Token;

typedef struct Tokens {
  Token* items;
  size_t count;
} Tokens;

int tokenize(char** program, Tokens* output);

#endif
