#include "parser.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "validator.h"

static bool matches(Tokens const* tokens, size_t index, char const* expected) {
  size_t const length = strlen(expected);
  return index < tokens->count && tokens->items[index].length == length &&
         memcmp(tokens->items[index].text, expected, length) == 0;
}

static int syntax_error(Tokens const* tokens, size_t index,
                        char const* message) {
  if (index < tokens->count) {
    fprintf(stderr, "Bosonoga token %zu (%.*s): %s\n", index + 1,
            (int)tokens->items[index].length, tokens->items[index].text,
            message);
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

static bool copy_theme_name(Token const* token,
                            char output[static BOSONO_THEME_NAME_LIMIT]) {
  if (token->length == 0 || token->length >= BOSONO_THEME_NAME_LIMIT ||
      token->text[0] < 'a' || token->text[0] > 'z')
    return false;
  for (size_t index = 0; index < token->length; index++) {
    char const value = token->text[index];
    if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
          value == '_'))
      return false;
  }
  memcpy(output, token->text, token->length);
  output[token->length] = '\0';
  return true;
}

static bool parse_color(Token const* token, uint32_t* output) {
  if (token->length != 11 || memcmp(token->text, "HEX_", 4) != 0 ||
      token->text[10] != 'U')
    return false;
  uint32_t color = 0;
  for (size_t index = 4; index < 10; index++) {
    char const digit = token->text[index];
    uint32_t value;
    if (digit >= '0' && digit <= '9')
      value = (uint32_t)(digit - '0');
    else if (digit >= 'A' && digit <= 'F')
      value = (uint32_t)(digit - 'A' + 10);
    else if (digit >= 'a' && digit <= 'f')
      value = (uint32_t)(digit - 'a' + 10);
    else
      return false;
    color = (color << 4) | value;
  }
  *output = color;
  return true;
}

static int parse_theme(Tokens const* tokens, size_t* position,
                       BosonoProgram* output) {
  if (output->theme_count >= BOSONO_THEME_LIMIT) {
    return syntax_error(
        tokens, *position,
        "Too many themes (maximum " BOSONO_TEXT(BOSONO_THEME_LIMIT) ")");
  }
  BosonoTheme* const theme = &output->themes[output->theme_count];
  (*position)++;
  if (*position >= tokens->count ||
      !copy_theme_name(&tokens->items[*position], theme->name)) {
    return syntax_error(tokens, *position, "Expected a lowercase theme name");
  }
  for (size_t index = 0; index < output->theme_count; index++) {
    if (strcmp(output->themes[index].name, theme->name) == 0) {
      return syntax_error(tokens, *position, "Duplicate theme name");
    }
  }
  (*position)++;
  for (size_t index = 0; index < BOSONO_THEME_COLOR_COUNT + 1; index++) {
    uint32_t* const color = index < BOSONO_THEME_COLOR_COUNT
                                ? &theme->rectangles[index]
                                : &theme->background;
    if (*position >= tokens->count ||
        !parse_color(&tokens->items[*position], color)) {
      return syntax_error(tokens, *position, "Expected RGB color HEX_RRGGBBU");
    }
    (*position)++;
  }
  output->theme_count++;
  return EXIT_SUCCESS;
}

typedef struct Variable {
  char name[BOSONO_VARIABLE_NAME_LIMIT];
  int32_t value;
} Variable;

static bool parse_i32(Token const* token, int32_t* output) {
  if (token->length <= 4 ||
      memcmp(token->text + token->length - 4, "_i32", 4) != 0)
    return false;
  size_t position = 0;
  size_t const end = token->length - 4;
  bool const negative = token->text[0] == '-';
  if (negative || token->text[0] == '+') position++;
  if (position == end) return false;
  uint32_t const limit = negative ? UINT32_C(2147483648) : INT32_MAX;
  uint32_t magnitude = 0;
  for (; position < end; position++) {
    char const digit = token->text[position];
    if (digit < '0' || digit > '9') return false;
    uint32_t const value = (uint32_t)(digit - '0');
    if (magnitude > (limit - value) / 10U) return false;
    magnitude = magnitude * 10U + value;
  }
  *output = negative ? (magnitude == UINT32_C(2147483648) ? INT32_MIN
                                                          : -(int32_t)magnitude)
                     : (int32_t)magnitude;
  return true;
}

static int parse_variable(Tokens const* tokens, size_t* position,
                          Variable* variables, size_t* count) {
  if (*count >= BOSONO_VARIABLE_LIMIT) {
    return syntax_error(tokens, *position, "Too many variables in key block");
  }
  (*position)++;
  Variable* const variable = &variables[*count];
  if (*position >= tokens->count ||
      !copy_theme_name(&tokens->items[*position], variable->name) ||
      matches(tokens, *position, "var") || matches(tokens, *position, "info") ||
      matches(tokens, *position, "start") || matches(tokens, *position, "do") ||
      matches(tokens, *position, "is") || matches(tokens, *position, "set") ||
      matches(tokens, *position, "to_add") ||
      matches(tokens, *position, "to_subtract")) {
    return syntax_error(tokens, *position, "Expected a variable name");
  }
  for (size_t index = 0; index < *count; index++) {
    if (strcmp(variables[index].name, variable->name) == 0) {
      return syntax_error(tokens, *position, "Duplicate variable declaration");
    }
  }
  (*position)++;
  if (!matches(tokens, *position, "is")) {
    return syntax_error(tokens, *position, "Expected is after variable name");
  }
  (*position)++;
  if (*position >= tokens->count ||
      !parse_i32(&tokens->items[*position], &variable->value)) {
    return syntax_error(tokens, *position,
                        "Expected a signed 32-bit integer with _i32 suffix");
  }
  (*position)++;
  (*count)++;
  return EXIT_SUCCESS;
}

static size_t find_variable(Token const* token, Variable const* variables,
                            size_t count) {
  for (size_t index = 0; index < count; index++) {
    size_t const length = strlen(variables[index].name);
    if (token->length == length &&
        memcmp(token->text, variables[index].name, length) == 0)
      return index;
  }
  return count;
}

static bool read_operand(Token const* token, Variable const* variables,
                         size_t count, int32_t* value) {
  size_t const index = find_variable(token, variables, count);
  if (index < count) {
    *value = variables[index].value;
    return true;
  }
  return parse_i32(token, value);
}

static int parse_set(Tokens const* tokens, size_t* position,
                     Variable* variables, size_t count) {
  (*position)++;
  if (*position >= tokens->count) {
    return syntax_error(tokens, *position, "Expected assignment target");
  }
  size_t const target =
      find_variable(&tokens->items[*position], variables, count);
  if (target == count) {
    return syntax_error(tokens, *position, "Unknown assignment target");
  }
  (*position)++;
  bool const addition = matches(tokens, *position, "to_add");
  if (!addition && !matches(tokens, *position, "to_subtract")) {
    return syntax_error(tokens, *position, "Expected to_add or to_subtract");
  }
  (*position)++;
  int32_t operands[2];
  for (size_t index = 0; index < 2; index++) {
    if (*position >= tokens->count ||
        !read_operand(&tokens->items[*position], variables, count,
                      &operands[index])) {
      return syntax_error(tokens, *position,
                          "Expected a declared variable or _i32 integer");
    }
    (*position)++;
  }
  int64_t const result = addition ? (int64_t)operands[0] + (int64_t)operands[1]
                                  : (int64_t)operands[0] - (int64_t)operands[1];
  if (result < INT32_MIN || result > INT32_MAX) {
    return syntax_error(tokens, *position - 1,
                        "Assignment result exceeds signed 32-bit range");
  }
  variables[target].value = (int32_t)result;
  return EXIT_SUCCESS;
}

static int parse_info(Tokens const* tokens, size_t* position,
                      Variable const* variables, size_t variable_count,
                      char output[static BOSONO_STRING_LIMIT]) {
  size_t length = 0;
  size_t argument_count = 0;
  (*position)++;
  while (*position < tokens->count && !matches(tokens, *position, "info") &&
         !matches(tokens, *position, "var") &&
         !matches(tokens, *position, "set") &&
         !matches(tokens, *position, "start") &&
         !matches(tokens, *position, "do")) {
    Token const* const token = &tokens->items[*position];
    char fragment[BOSONO_STRING_LIMIT];
    if (token->length != 0 && token->text[0] == '"') {
      if (!copy_string(token, fragment)) {
        return syntax_error(tokens, *position,
                            "Expected a valid quoted UTF-8 string");
      }
    } else {
      size_t index = 0;
      for (; index < variable_count; index++) {
        size_t const name_length = strlen(variables[index].name);
        if (token->length == name_length &&
            memcmp(token->text, variables[index].name, name_length) == 0)
          break;
      }
      if (index == variable_count) {
        return syntax_error(tokens, *position,
                            "Unknown variable in info statement");
      }
      (void)snprintf(fragment, sizeof fragment, "%" PRId32,
                     variables[index].value);
    }
    size_t const fragment_length = strlen(fragment);
    if (fragment_length > BOSONO_STRING_MAX_BYTES - length) {
      return syntax_error(tokens, *position,
                          "Concatenated info text exceeds string limit");
    }
    memcpy(output + length, fragment, fragment_length);
    length += fragment_length;
    argument_count++;
    (*position)++;
  }
  if (argument_count == 0) {
    return syntax_error(tokens, *position,
                        "Expected a string or variable after info");
  }
  output[length] = '\0';
  return EXIT_SUCCESS;
}

static int parse_rules(Tokens const* tokens, BosonoProgram* output) {
  size_t position = 0;
  while (position < tokens->count) {
    if (matches(tokens, position, "theme")) {
      if (parse_theme(tokens, &position, output) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
      }
      continue;
    }
    if (output->rule_count >= BOSONO_RULE_LIMIT) {
      return syntax_error(
          tokens, position,
          "Too many key blocks (maximum " BOSONO_TEXT(BOSONO_RULE_LIMIT) ")");
    }
    Token const* const trigger = &tokens->items[position];
    bool const is_press =
        trigger->length == 7 && strncmp(trigger->text, "press_", 6) == 0;
    bool const is_down =
        trigger->length == 6 && strncmp(trigger->text, "down_", 5) == 0;
    if (!is_press && !is_down) {
      return syntax_error(tokens, position,
                          "Expected press_<key> or down_<key>");
    }
    char const key = trigger->text[trigger->length - 1];
    if (!((key >= 'a' && key <= 'z') || (key >= '0' && key <= '9'))) {
      return syntax_error(tokens, position,
                          "Expected a lowercase letter or digit");
    }
    BosonoRule* const rule = &output->rules[output->rule_count];
    rule->key = key;
    rule->start = false;
    rule->info_count = 0;
    rule->trigger = is_press ? BOSONO_TRIGGER_PRESS : BOSONO_TRIGGER_DOWN;
    for (size_t index = 0; index < output->rule_count; index++) {
      if (output->rules[index].key == rule->key &&
          output->rules[index].trigger == rule->trigger) {
        return syntax_error(tokens, position, "Duplicate key block");
      }
    }
    position++;
    if (!matches(tokens, position, "di")) {
      return syntax_error(tokens, position, "Expected di after key trigger");
    }
    position++;
    Variable variables[BOSONO_VARIABLE_LIMIT];
    size_t variable_count = 0;
    while (matches(tokens, position, "info") ||
           matches(tokens, position, "start") ||
           matches(tokens, position, "var") ||
           matches(tokens, position, "set")) {
      if (matches(tokens, position, "set")) {
        if (parse_set(tokens, &position, variables, variable_count) !=
            EXIT_SUCCESS) {
          return EXIT_FAILURE;
        }
        continue;
      }
      if (matches(tokens, position, "var")) {
        if (parse_variable(tokens, &position, variables, &variable_count) !=
            EXIT_SUCCESS) {
          return EXIT_FAILURE;
        }
        continue;
      }
      if (matches(tokens, position, "start")) {
        if (rule->start) {
          return syntax_error(tokens, position, "Duplicate start action");
        }
        rule->start = true;
        position++;
        continue;
      }
      if (rule->info_count >= BOSONO_RULE_INFO_LIMIT) {
        return syntax_error(tokens, position,
                            "Too many info lines (maximum " BOSONO_TEXT(
                                BOSONO_RULE_INFO_LIMIT) ")");
      }
      if (parse_info(tokens, &position, variables, variable_count,
                     rule->info[rule->info_count]) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
      }
      rule->info_count++;
    }
    if (rule->info_count == 0 && !rule->start) {
      return syntax_error(tokens, position,
                          "Expected at least one info or start statement");
    }
    if (!matches(tokens, position, "do")) {
      return syntax_error(
          tokens, position,
          "Expected info, var, set, start, or do to close key block");
    }
    position++;
    output->rule_count++;
  }
  return EXIT_SUCCESS;
}

int parse(Tokens const* tokens, BosonoProgram* output) {
  output->theme_count = 0;
  output->rule_count = 0;
  int result = parse_rules(tokens, output);
  if (result == EXIT_SUCCESS && output->theme_count < BOSONO_THEME_MIN) {
    result = syntax_error(tokens, tokens->count,
                          "Expected at least " BOSONO_TEXT(
                              BOSONO_THEME_MIN) " theme declaration");
  }
  if (result != EXIT_SUCCESS) {
    output->rule_count = 0;
    output->theme_count = 0;
  }
  return result;
}
