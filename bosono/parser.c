#include "parser.h"

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
    while (matches(tokens, position, "info") ||
           matches(tokens, position, "start")) {
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
      position++;
      if (position >= tokens->count ||
          !copy_string(&tokens->items[position],
                       rule->info[rule->info_count])) {
        return syntax_error(tokens, position,
                            "Expected a quoted UTF-8 string of at most "
                            BOSONO_TEXT(BOSONO_STRING_MAX_BYTES) " bytes with "
                            "valid escapes");
      }
      rule->info_count++;
      position++;
    }
    if (rule->info_count == 0 && !rule->start) {
      return syntax_error(tokens, position,
                          "Expected at least one info or start statement");
    }
    if (!matches(tokens, position, "do")) {
      return syntax_error(tokens, position,
                          "Expected info, start, or do to close key block");
    }
    position++;
    output->rule_count++;
  }
  return EXIT_SUCCESS;
}

int parse(Tokens const* tokens, BosonoProgram* output) {
  output->theme_count = 0;
  output->rule_count = 0;
  int const result = parse_rules(tokens, output);
  if (result != EXIT_SUCCESS) {
    output->rule_count = 0;
    output->theme_count = 0;
  }
  return result;
}
