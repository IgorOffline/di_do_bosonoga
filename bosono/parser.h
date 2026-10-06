#ifndef BOSONO_PARSER_H
#define BOSONO_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "tokenizer.h"

typedef enum BosonoTrigger {
  BOSONO_TRIGGER_INPUT_KEY_PRESS,
  BOSONO_TRIGGER_INPUT_KEY_DOWN
} BosonoTrigger;

typedef struct BosonoRule {
  char key;
  BosonoTrigger trigger;
  bool start;
  char info[BOSONO_RULE_INFO_LIMIT][BOSONO_STRING_LIMIT];
  size_t info_count;
  size_t variable_start;
  size_t variable_count;
} BosonoRule;

typedef enum BosonoValueKind {
  BOSONO_VALUE_I32,
  BOSONO_VALUE_FLAGS,
  BOSONO_VALUE_BOOL,
  BOSONO_VALUE_PATH
} BosonoValueKind;

typedef struct BosonoValue {
  BosonoValueKind kind;
  union {
    int32_t i32;
    uint32_t flags;
    bool boolean;
    char path[BOSONO_STRING_LIMIT];
  };
} BosonoValue;

typedef struct BosonoVariable {
  char name[BOSONO_VARIABLE_NAME_LIMIT];
  BosonoValue value;
} BosonoVariable;

#define BOSONO_VARIABLE_LIMIT \
  ((BOSONO_VARIABLE_BYTES - sizeof(size_t)) / sizeof(BosonoVariable))

typedef struct BosonoVariables {
  size_t count;
  BosonoVariable items[BOSONO_VARIABLE_LIMIT];
} BosonoVariables;

typedef struct BosonoTheme {
  char name[BOSONO_THEME_NAME_LIMIT];
  uint32_t rectangles[BOSONO_THEME_COLOR_COUNT];
  size_t assets[BOSONO_THEME_COLOR_COUNT];
  bool uses_assets;
  uint32_t background;
} BosonoTheme;

typedef struct BosonoAsset {
  char name[BOSONO_THEME_NAME_LIMIT];
  char filename[BOSONO_ASSET_PATH_LIMIT];
} BosonoAsset;

typedef struct BosonoProgram {
  char startup_info[16][BOSONO_STRING_LIMIT];
  size_t startup_info_count;
  BosonoAsset assets[BOSONO_ASSET_LIMIT];
  size_t asset_count;
  BosonoTheme themes[BOSONO_THEME_LIMIT];
  size_t theme_count;
  BosonoRule rules[BOSONO_RULE_LIMIT];
  size_t rule_count;
} BosonoProgram;

int parse(Tokens const* tokens, BosonoProgram* output,
          BosonoVariables* variables, bool random_binary);

int parse_with_aliases(Tokens const* tokens, BosonoProgram* output,
                       BosonoVariables* variables, bool random_binary,
                       BosonoAliases const* aliases);

#endif
