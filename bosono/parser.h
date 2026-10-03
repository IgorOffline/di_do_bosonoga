#ifndef BOSONO_PARSER_H
#define BOSONO_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "tokenizer.h"

typedef enum BosonoTrigger {
  BOSONO_TRIGGER_PRESS,
  BOSONO_TRIGGER_DOWN
} BosonoTrigger;

typedef struct BosonoRule {
  char key;
  BosonoTrigger trigger;
  bool start;
  char info[BOSONO_RULE_INFO_LIMIT][BOSONO_STRING_LIMIT];
  size_t info_count;
} BosonoRule;

typedef struct BosonoTheme {
  char name[BOSONO_THEME_NAME_LIMIT];
  uint32_t rectangles[BOSONO_THEME_COLOR_COUNT];
  uint32_t background;
} BosonoTheme;

typedef struct BosonoProgram {
  BosonoTheme themes[BOSONO_THEME_LIMIT];
  size_t theme_count;
  BosonoRule rules[BOSONO_RULE_LIMIT];
  size_t rule_count;
} BosonoProgram;

int parse(Tokens const* tokens, BosonoProgram* output);

#endif
