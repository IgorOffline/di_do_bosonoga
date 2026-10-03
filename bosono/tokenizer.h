#ifndef BOSONO_TOKENIZER_H
#define BOSONO_TOKENIZER_H

#include <stddef.h>

#include "bosono.h"

typedef struct Token {
  char const* text;
  size_t length;
} Token;

typedef struct Tokens {
  Token* items;
  size_t count;
} Tokens;

int tokenize(char const program[static BOSONO_LIMIT], Tokens* output);

#endif
