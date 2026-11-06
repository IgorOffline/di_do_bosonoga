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

#define BOSONO_ALIAS_LIMIT 64
#define BOSONO_ALIAS_NAME_LIMIT 64
typedef enum BosonoAliasKind {
  BOSONO_ALIAS_EXACT,
  BOSONO_ALIAS_PREFIX,
  BOSONO_ALIAS_SUFFIX
} BosonoAliasKind;

typedef struct BosonoAlias {
  BosonoAliasKind kind;
  char original[BOSONO_ALIAS_NAME_LIMIT];
  char shorter[BOSONO_ALIAS_NAME_LIMIT];
} BosonoAlias;
typedef struct BosonoAliases {
  BosonoAlias items[BOSONO_ALIAS_LIMIT];
  size_t count;
} BosonoAliases;
int load_aliases(Tokens const* tokens, BosonoAliases* aliases);
int validate_aliases(Tokens const* legal, BosonoAliases const* aliases);
int apply_aliases(char program[static BOSONO_LIMIT], Tokens* tokens,
                  BosonoAliases const* aliases);

int tokenize(char const program[static BOSONO_LIMIT], Tokens* output);

#endif
