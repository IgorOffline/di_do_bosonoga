#ifndef BOSONO_PARSER_H
#define BOSONO_PARSER_H

#include "tokenizer.h"

typedef struct BosonoRule {
  char key;
  char info[BOSONO_RULE_INFO_LIMIT][BOSONO_STRING_LIMIT];
  size_t info_count;
} BosonoRule;

typedef struct BosonoProgram {
  BosonoRule rules[BOSONO_RULE_LIMIT];
  size_t rule_count;
} BosonoProgram;

int parse(Tokens* tokens, BosonoProgram* output);

#endif
