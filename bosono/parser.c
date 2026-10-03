#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "validator.h"

static bool matches(Tokens const* tokens, size_t index, char const* expected) {
  return index < tokens->count &&
         strcmp(tokens->items[index].text, expected) == 0;
}

static int syntax_error(Tokens const* tokens, size_t index,
                        char const* message) {
  if (index < tokens->count) {
    fprintf(stderr, "Bosonoga token %zu (%s): %s\n", index + 1,
            tokens->items[index].text, message);
  } else {
    fprintf(stderr, "Bosonoga end of file: %s\n", message);
  }
  return EXIT_FAILURE;
}

static bool copy_string(Token const* token,
                        char output[static BOSONO_STRING_LIMIT]) {
  if (token->length < 2 || token->text[0] != '"' ||
      token->text[token->length - 1] != '"')
    return false;
  size_t length = 0;
  for (size_t index = 1; index < token->length - 1; index++) {
    char value = token->text[index];
    if (value == '"') return false;
    if (value == '\\') {
      index++;
      if (index >= token->length - 1) return false;
      switch (token->text[index]) {
        case '"':
          value = '"';
          break;
        case '\\':
          value = '\\';
          break;
        case 'n':
          value = '\n';
          break;
        case 'r':
          value = '\r';
          break;
        case 't':
          value = '\t';
          break;
        default:
          return false;
      }
    }
    if (length >= BOSONO_STRING_LIMIT - 1) return false;
    output[length++] = value;
  }
  output[length] = '\0';
  return valid_utf8(output, length);
}

static int parse_rules(Tokens const* tokens, BosonoProgram* output) {
  size_t position = 0;
  while (position < tokens->count) {
    if (output->rule_count >= BOSONO_RULE_LIMIT) {
      return syntax_error(tokens, position, "Too many key blocks (maximum 8)");
    }
    Token const* const trigger = &tokens->items[position];
    if (trigger->length != 7 || strncmp(trigger->text, "press_", 6) != 0 ||
        trigger->text[6] < 'a' || trigger->text[6] > 'z') {
      return syntax_error(tokens, position,
                          "Expected press_<lowercase letter>");
    }
    BosonoRule* const rule = &output->rules[output->rule_count];
    rule->key = trigger->text[6];
    for (size_t index = 0; index < output->rule_count; index++) {
      if (output->rules[index].key == rule->key) {
        return syntax_error(tokens, position, "Duplicate key block");
      }
    }
    position++;
    if (!matches(tokens, position, "di")) {
      return syntax_error(tokens, position, "Expected di after key trigger");
    }
    position++;
    while (matches(tokens, position, "info")) {
      if (rule->info_count >= BOSONO_RULE_INFO_LIMIT) {
        return syntax_error(tokens, position,
                            "Too many info lines (maximum 4)");
      }
      position++;
      if (position >= tokens->count ||
          !copy_string(&tokens->items[position],
                       rule->info[rule->info_count])) {
        return syntax_error(tokens, position,
                            "Expected a quoted UTF-8 string of at most 127 "
                            "bytes with valid escapes");
      }
      rule->info_count++;
      position++;
    }
    if (rule->info_count == 0) {
      return syntax_error(tokens, position,
                          "Expected at least one info statement");
    }
    if (!matches(tokens, position, "do")) {
      return syntax_error(tokens, position,
                          "Expected info or do to close key block");
    }
    position++;
    output->rule_count++;
  }
  return EXIT_SUCCESS;
}

int parse(Tokens* tokens, BosonoProgram* output) {
  *output = (BosonoProgram){0};
  int const result = parse_rules(tokens, output);
  for (size_t index = 0; index < tokens->count; index++) {
    free(tokens->items[index].text);
  }
  free(tokens->items);
  *tokens = (Tokens){0};
  if (result != EXIT_SUCCESS) *output = (BosonoProgram){0};
  return result;
}
