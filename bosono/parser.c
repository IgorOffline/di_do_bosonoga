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
  return BOSONOGA_EXIT_FAILURE;
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

static int parse_asset(Tokens const* tokens, size_t* position,
                       BosonoProgram* output) {
  if (output->asset_count >= BOSONO_ASSET_LIMIT) {
    return syntax_error(tokens, *position, "Too many assets");
  }
  BosonoAsset* const asset = &output->assets[output->asset_count];
  (*position)++;
  if (*position >= tokens->count ||
      !copy_theme_name(&tokens->items[*position], asset->name)) {
    return syntax_error(tokens, *position, "Expected an asset name");
  }
  for (size_t index = 0; index < output->asset_count; index++) {
    if (strcmp(output->assets[index].name, asset->name) == 0) {
      return syntax_error(tokens, *position, "Duplicate asset name");
    }
  }
  (*position)++;
  if (*position >= tokens->count) {
    return syntax_error(tokens, *position, "Expected a PNG filename in asset/");
  }
  Token filename = tokens->items[*position];
  if (filename.length < 2 || filename.text[0] != '"' ||
      filename.text[filename.length - 1] != '"') {
    return syntax_error(tokens, *position, "Expected a quoted PNG filename");
  }
  filename.text++;
  filename.length -= 2;
  Token const* const path = &filename;
  if (path->length < 5 || path->length >= sizeof asset->filename ||
      memcmp(path->text + path->length - 4, ".png", 4) != 0) {
    return syntax_error(tokens, *position, "Expected a PNG filename in asset/");
  }
  for (size_t index = 0; index < path->length; index++) {
    char const value = path->text[index];
    if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
          (value >= '0' && value <= '9') || value == '_' || value == '-' ||
          value == '.')) {
      return syntax_error(tokens, *position,
                          "Expected a PNG filename in asset/");
    }
  }
  memcpy(asset->filename, path->text, path->length);
  asset->filename[path->length] = '\0';
  (*position)++;
  output->asset_count++;
  return BOSONOGA_EXIT_SUCCESS;
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
  if (*position >= tokens->count) {
    return syntax_error(tokens, *position, "Expected a quoted theme name");
  }
  Token quoted_name = tokens->items[*position];
  if (quoted_name.length < 2 || quoted_name.text[0] != '"' ||
      quoted_name.text[quoted_name.length - 1] != '"') {
    return syntax_error(tokens, *position, "Expected a quoted theme name");
  }
  quoted_name.text++;
  quoted_name.length -= 2;
  if (!copy_theme_name(&quoted_name, theme->name)) {
    return syntax_error(tokens, *position,
                        "Expected a lowercase quoted theme name");
  }
  for (size_t index = 0; index < output->theme_count; index++) {
    if (strcmp(output->themes[index].name, theme->name) == 0) {
      return syntax_error(tokens, *position, "Duplicate theme name");
    }
  }
  (*position)++;
  theme->uses_assets = false;
  for (size_t index = 0; index < BOSONO_THEME_COLOR_COUNT + 1; index++) {
    uint32_t* const color = index < BOSONO_THEME_COLOR_COUNT
                                ? &theme->rectangles[index]
                                : &theme->background;
    if (*position >= tokens->count) {
      return syntax_error(tokens, *position, "Incomplete theme declaration");
    }
    bool const is_color = parse_color(&tokens->items[*position], color);
    if (index == 0) theme->uses_assets = !is_color;
    if (index < BOSONO_THEME_COLOR_COUNT && theme->uses_assets) {
      Token const* const name = &tokens->items[*position];
      size_t asset_index = 0;
      for (; asset_index < output->asset_count; asset_index++) {
        size_t const length = strlen(output->assets[asset_index].name);
        if (name->length == length &&
            memcmp(name->text, output->assets[asset_index].name, length) == 0)
          break;
      }
      if (asset_index == output->asset_count) {
        return syntax_error(tokens, *position,
                            "Expected an asset declared before the theme");
      }
      theme->assets[index] = asset_index;
      theme->rectangles[index] = UINT32_C(0xffffff);
    } else if (!is_color) {
      return syntax_error(tokens, *position, "Expected RGB color HEX_RRGGBBU");
    }
    (*position)++;
  }
  output->theme_count++;
  return BOSONOGA_EXIT_SUCCESS;
}

static bool parse_i32(Token const* token, int32_t* output) {
  size_t const suffix_length = sizeof "_bosonoga_i32" - 1;
  if (token->length <= suffix_length ||
      memcmp(token->text + token->length - suffix_length, "_bosonoga_i32",
             suffix_length) != 0)
    return false;
  size_t position = 0;
  size_t const end = token->length - suffix_length;
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

static size_t find_variable(Token const* token, BosonoVariable const* variables,
                            size_t count) {
  for (size_t index = 0; index < count; index++) {
    size_t const length = strlen(variables[index].name);
    if (token->length == length &&
        memcmp(token->text, variables[index].name, length) == 0)
      return index;
  }
  return count;
}

static bool read_operand(Token const* token, BosonoVariable const* variables,
                         size_t count, int32_t* value) {
  size_t const index = find_variable(token, variables, count);
  if (index < count) {
    if (variables[index].value.kind != BOSONO_VALUE_I32) return false;
    *value = variables[index].value.i32;
    return true;
  }
  return parse_i32(token, value);
}

static bool read_flags(Token const* token, BosonoVariable const* variables,
                       size_t count, uint32_t* flags) {
  size_t const index = find_variable(token, variables, count);
  if (index < count) {
    if (variables[index].value.kind != BOSONO_VALUE_FLAGS) return false;
    *flags = variables[index].value.flags;
    return true;
  }
  size_t const prefix_length = sizeof "_bosonoga_shift_" - 1;
  if (token->length < prefix_length + 1 || token->length > prefix_length + 2 ||
      memcmp(token->text, "_bosonoga_shift_", prefix_length) != 0)
    return false;
  unsigned int bit = 0;
  for (size_t digit = prefix_length; digit < token->length; digit++) {
    if (token->text[digit] < '0' || token->text[digit] > '9') return false;
    bit = bit * 10U + (unsigned int)(token->text[digit] - '0');
  }
  if (bit >= 32) return false;
  *flags = UINT32_C(1) << bit;
  return true;
}

typedef struct BosonoExecution {
  BosonoAliases const* aliases;
  size_t import_depth;
  bool imported;
  bool returned;
  int32_t result;
} BosonoExecution;

static int execute_import(char const* filename, BosonoExecution* execution,
                          int32_t* result);

static int parse_variable(Tokens const* tokens, size_t* position,
                          BosonoVariable* variables, size_t* count,
                          size_t capacity, bool random_binary, bool active,
                          BosonoExecution* execution) {
  if (*count >= capacity) {
    return syntax_error(tokens, *position, "Regina variable storage is full");
  }
  (*position)++;
  BosonoVariable* const variable = &variables[*count];
  if (*position >= tokens->count ||
      !copy_theme_name(&tokens->items[*position], variable->name) ||
      matches(tokens, *position, "_bosonoga_var") ||
      matches(tokens, *position, "_bosonoga_info") ||
      matches(tokens, *position, "_bosonoga_start") ||
      matches(tokens, *position, "_bosonoga_do") ||
      matches(tokens, *position, "_bosonoga_is") ||
      matches(tokens, *position, "_bosonoga_set") ||
      matches(tokens, *position, "_bosonoga_to_add") ||
      matches(tokens, *position, "_bosonoga_to_subtract") ||
      matches(tokens, *position, "_bosonoga_if") ||
      matches(tokens, *position, "_bosonoga_else") ||
      matches(tokens, *position, "_bosonoga_eq") ||
      matches(tokens, *position, "_bosonoga_not_eq") ||
      matches(tokens, *position, "_bosonoga_di") ||
      matches(tokens, *position, "_bosonoga_shift_or") ||
      matches(tokens, *position, "_bosonoga_shift_eq") ||
      matches(tokens, *position, "_bosonoga_gt") ||
      matches(tokens, *position, "_bosonoga_lt") ||
      matches(tokens, *position, "_bosonoga_nihil") ||
      matches(tokens, *position, "_bosonoga_raw_import_and_execute") ||
      matches(tokens, *position, "_bosonoga_return") ||
      matches(tokens, *position, "_bosonoga_exit_success") ||
      matches(tokens, *position, "_bosonoga_exit_failure")) {
    return syntax_error(tokens, *position, "Expected a variable name");
  }
  for (size_t index = 0; index < *count; index++) {
    if (strcmp(variables[index].name, variable->name) == 0) {
      return syntax_error(tokens, *position, "Duplicate variable declaration");
    }
  }
  (*position)++;
  if (!matches(tokens, *position, "_bosonoga_is")) {
    return syntax_error(tokens, *position, "Expected is after variable name");
  }
  (*position)++;
  if (*position >= tokens->count) {
    return syntax_error(tokens, *position, "Expected a variable initializer");
  }
  int32_t integer;
  uint32_t flags;
  if (matches(tokens, *position, "_bosonoga_raw_import_and_execute")) {
    (*position)++;
    char filename[BOSONO_STRING_LIMIT];
    if (*position >= tokens->count ||
        !copy_string(&tokens->items[*position], filename) || !filename[0])
      return syntax_error(tokens, *position, "Expected an import filename");
    variable->value.kind = BOSONO_VALUE_I32;
    variable->value.i32 = 0;
    if (active && execute_import(filename, execution, &variable->value.i32) !=
                      BOSONOGA_EXIT_SUCCESS)
      return syntax_error(tokens, *position,
                          "Unable to execute imported script");
    (*position)++;
  } else if (matches(tokens, *position, "default_rand_binary") ||
             matches(tokens, *position, "true") ||
             matches(tokens, *position, "false")) {
    variable->value.kind = BOSONO_VALUE_BOOL;
    variable->value.boolean = matches(tokens, *position, "default_rand_binary")
                                  ? random_binary
                                  : matches(tokens, *position, "true");
    (*position)++;
  } else if (parse_i32(&tokens->items[*position], &integer)) {
    variable->value.kind = BOSONO_VALUE_I32;
    variable->value.i32 = integer;
    (*position)++;
  } else if (read_flags(&tokens->items[*position], variables, *count, &flags)) {
    variable->value.kind = BOSONO_VALUE_FLAGS;
    (*position)++;
    while (matches(tokens, *position, "_bosonoga_shift_or")) {
      (*position)++;
      uint32_t operand;
      if (*position >= tokens->count ||
          !read_flags(&tokens->items[*position], variables, *count, &operand)) {
        return syntax_error(tokens, *position,
                            "Expected a flag variable or _bosonoga_shift_0 "
                            "through _bosonoga_shift_31");
      }
      flags |= operand;
      (*position)++;
    }
    variable->value.flags = flags;
  } else {
    return syntax_error(tokens, *position,
                        "Expected a _bosonoga_i32 integer, flag variable, or "
                        "_bosonoga_shift_0 through _bosonoga_shift_31");
  }
  (*count)++;
  return BOSONOGA_EXIT_SUCCESS;
}

static int parse_set(Tokens const* tokens, size_t* position,
                     BosonoVariable* variables, size_t count, bool active) {
  (*position)++;
  if (*position >= tokens->count) {
    return syntax_error(tokens, *position, "Expected assignment target");
  }
  size_t const target =
      find_variable(&tokens->items[*position], variables, count);
  if (target == count) {
    return syntax_error(tokens, *position, "Unknown assignment target");
  }
  if (variables[target].value.kind != BOSONO_VALUE_I32) {
    return syntax_error(
        tokens, *position,
        "Arithmetic assignment requires a _bosonoga_i32 target");
  }
  (*position)++;
  bool const addition = matches(tokens, *position, "_bosonoga_to_add");
  if (!addition && !matches(tokens, *position, "_bosonoga_to_subtract")) {
    return syntax_error(tokens, *position, "Expected to_add or to_subtract");
  }
  (*position)++;
  size_t operand_count = 0;
  int64_t result = 0;
  while (*position < tokens->count &&
         !matches(tokens, *position, "_bosonoga_info") &&
         !matches(tokens, *position, "_bosonoga_var") &&
         !matches(tokens, *position, "_bosonoga_set") &&
         !matches(tokens, *position, "_bosonoga_start") &&
         !matches(tokens, *position, "_bosonoga_do") &&
         !matches(tokens, *position, "_bosonoga_if") &&
         !matches(tokens, *position, "_bosonoga_else") &&
         !matches(tokens, *position, "_bosonoga_nihil") &&
         !matches(tokens, *position, "_bosonoga_return")) {
    int32_t operand;
    if (!read_operand(&tokens->items[*position], variables, count, &operand)) {
      return syntax_error(
          tokens, *position,
          "Expected a declared variable or _bosonoga_i32 integer");
    }
    if (operand_count == 0)
      result = operand;
    else if (addition)
      result += operand;
    else
      result -= operand;
    if (active && (result < INT32_MIN || result > INT32_MAX)) {
      return syntax_error(tokens, *position,
                          "Assignment result exceeds signed 32-bit range");
    }
    operand_count++;
    (*position)++;
  }
  if (operand_count < 2) {
    return syntax_error(tokens, *position,
                        "Expected at least two arithmetic operands");
  }
  if (active) variables[target].value.i32 = (int32_t)result;
  return BOSONOGA_EXIT_SUCCESS;
}

static int parse_info(Tokens const* tokens, size_t* position,
                      BosonoVariable const* variables, size_t variable_count,
                      char output[static BOSONO_STRING_LIMIT]) {
  size_t length = 0;
  size_t argument_count = 0;
  (*position)++;
  while (*position < tokens->count &&
         !matches(tokens, *position, "_bosonoga_info") &&
         !matches(tokens, *position, "_bosonoga_var") &&
         !matches(tokens, *position, "_bosonoga_set") &&
         !matches(tokens, *position, "_bosonoga_start") &&
         !matches(tokens, *position, "_bosonoga_do") &&
         !matches(tokens, *position, "_bosonoga_if") &&
         !matches(tokens, *position, "_bosonoga_else") &&
         !matches(tokens, *position, "_bosonoga_nihil") &&
         !matches(tokens, *position, "_bosonoga_return")) {
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
      if (variables[index].value.kind == BOSONO_VALUE_BOOL) {
        (void)snprintf(fragment, sizeof fragment, "%s",
                       variables[index].value.boolean ? "true" : "false");
      } else if (variables[index].value.kind == BOSONO_VALUE_FLAGS) {
        (void)snprintf(fragment, sizeof fragment, "%" PRIu32,
                       variables[index].value.flags);
      } else {
        (void)snprintf(fragment, sizeof fragment, "%" PRId32,
                       variables[index].value.i32);
      }
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
  return BOSONOGA_EXIT_SUCCESS;
}

static bool read_condition_value(Token const* token,
                                 BosonoVariable const* variables, size_t count,
                                 int32_t* value) {
  if (read_operand(token, variables, count, value)) return true;
  if (token->length == 0 || token->length > 11) return false;
  size_t const suffix_length = sizeof "_bosonoga_i32" - 1;
  char literal[11 + sizeof "_bosonoga_i32"];
  memcpy(literal, token->text, token->length);
  memcpy(literal + token->length, "_bosonoga_i32", suffix_length);
  Token const integer = {.text = literal,
                         .length = token->length + suffix_length};
  return parse_i32(&integer, value);
}

static int parse_statements(Tokens const* tokens, size_t* position,
                            BosonoRule* rule, BosonoVariables* storage,
                            size_t* variable_count, bool active, size_t depth,
                            BosonoExecution* execution) {
  if (depth > BOSONO_CONDITION_DEPTH_LIMIT) {
    return syntax_error(tokens, *position,
                        "Conditional nesting limit exceeded");
  }
  BosonoVariable* const variables = storage->items + rule->variable_start;
  size_t const capacity = BOSONO_VARIABLE_LIMIT - rule->variable_start;
  while (*position < tokens->count &&
         !matches(tokens, *position, "_bosonoga_do")) {
    bool const executing = active && !execution->returned;
    if (matches(tokens, *position, "_bosonoga_if")) {
      (*position)++;
      bool condition = false;
      do {
        if (matches(tokens, *position, "or")) (*position)++;
        bool const flags = matches(tokens, *position + 1, "_bosonoga_shift_eq");
        int32_t left = 0, right = 0;
        uint32_t left_flags = 0, right_flags = 0;
        if (*position >= tokens->count ||
            !(flags ? read_flags(&tokens->items[*position], variables,
                                 *variable_count, &left_flags)
                    : read_condition_value(&tokens->items[*position], variables,
                                           *variable_count, &left)))
          return syntax_error(tokens, *position,
                              "Expected a variable or integer in condition");
        (*position)++;
        bool const eq = matches(tokens, *position, "_bosonoga_eq");
        bool const ne = matches(tokens, *position, "_bosonoga_not_eq");
        bool const gt = matches(tokens, *position, "_bosonoga_gt");
        bool const lt = matches(tokens, *position, "_bosonoga_lt");
        if (!(eq || ne || gt || lt || flags))
          return syntax_error(tokens, *position,
                              "Expected eq, not_eq, gt, lt, or shift_eq");
        (*position)++;
        if (*position >= tokens->count ||
            !(flags ? read_flags(&tokens->items[*position], variables,
                                 *variable_count, &right_flags)
                    : read_condition_value(&tokens->items[*position], variables,
                                           *variable_count, &right)))
          return syntax_error(tokens, *position,
                              "Expected a variable or integer in condition");
        (*position)++;
        bool comparison = flags ? (left_flags & right_flags) == right_flags
                          : ne  ? left != right
                          : gt  ? left > right
                          : lt  ? left < right
                                : left == right;
        condition = condition || comparison;
      } while (matches(tokens, *position, "or"));
      if (!matches(tokens, *position, "_bosonoga_di"))
        return syntax_error(tokens, *position, "Expected di after condition");
      (*position)++;
      size_t const saved_count = *variable_count;
      if (parse_statements(tokens, position, rule, storage, variable_count,
                           executing && condition, depth + 1,
                           execution) != BOSONOGA_EXIT_SUCCESS)
        return BOSONOGA_EXIT_FAILURE;
      if (!active || !condition) *variable_count = saved_count;
      if (matches(tokens, *position, "_bosonoga_else")) {
        (*position)++;
        if (!matches(tokens, *position, "_bosonoga_di")) {
          return syntax_error(tokens, *position, "Expected di after else");
        }
        (*position)++;
        size_t const before_else = *variable_count;
        if (parse_statements(tokens, position, rule, storage, variable_count,
                             executing && !condition, depth + 1,
                             execution) != BOSONOGA_EXIT_SUCCESS)
          return BOSONOGA_EXIT_FAILURE;
        if (!active || condition) *variable_count = before_else;
      }
    } else if (matches(tokens, *position, "_bosonoga_return")) {
      if (!execution->imported)
        return syntax_error(tokens, *position,
                            "Return requires an imported script");
      (*position)++;
      int32_t result;
      if (matches(tokens, *position, "_bosonoga_exit_success"))
        result = BOSONOGA_EXIT_SUCCESS;
      else if (matches(tokens, *position, "_bosonoga_exit_failure"))
        result = BOSONOGA_EXIT_FAILURE;
      else
        return syntax_error(
            tokens, *position,
            "Expected exit_success or exit_failure after return");
      (*position)++;
      if (executing) {
        execution->returned = true;
        execution->result = result;
      }
    } else if (matches(tokens, *position, "_bosonoga_nihil")) {
      (*position)++;
    } else if (matches(tokens, *position, "_bosonoga_set")) {
      if (parse_set(tokens, position, variables, *variable_count, executing) !=
          BOSONOGA_EXIT_SUCCESS)
        return BOSONOGA_EXIT_FAILURE;
    } else if (matches(tokens, *position, "_bosonoga_var")) {
      if (parse_variable(tokens, position, variables, variable_count, capacity,
                         false, executing, execution) != BOSONOGA_EXIT_SUCCESS)
        return BOSONOGA_EXIT_FAILURE;
    } else if (matches(tokens, *position, "_bosonoga_start")) {
      if (executing && rule->start)
        return syntax_error(tokens, *position, "Duplicate start action");
      if (executing) rule->start = true;
      (*position)++;
    } else if (matches(tokens, *position, "_bosonoga_info")) {
      if (executing && rule->info_count >= BOSONO_RULE_INFO_LIMIT) {
        return syntax_error(tokens, *position, "Too many info statements");
      }
      char ignored[BOSONO_STRING_LIMIT];
      char* const text = executing ? rule->info[rule->info_count] : ignored;
      if (parse_info(tokens, position, variables, *variable_count, text) !=
          BOSONOGA_EXIT_SUCCESS)
        return BOSONOGA_EXIT_FAILURE;
      if (executing) rule->info_count++;
    } else {
      return syntax_error(tokens, *position,
                          "Expected if, info, var, set, start, return, or do");
    }
    storage->count = rule->variable_start + *variable_count;
    rule->variable_count = *variable_count;
  }
  if (!matches(tokens, *position, "_bosonoga_do")) {
    return syntax_error(tokens, *position, "Expected do to close block");
  }
  (*position)++;
  return BOSONOGA_EXIT_SUCCESS;
}

static int parse_rules(Tokens const* tokens, BosonoProgram* output,
                       BosonoVariables* storage, bool random_binary,
                       size_t depth, BosonoExecution* execution) {
  if (depth > BOSONO_CONDITION_DEPTH_LIMIT)
    return syntax_error(tokens, 0, "Conditional nesting limit exceeded");
  size_t position = 0;
  while (position < tokens->count) {
    if (matches(tokens, position, "_bosonoga_nihil")) {
      position++;
      continue;
    }
    if (matches(tokens, position, "_bosonoga_var")) {
      if (parse_variable(tokens, &position, storage->items, &storage->count,
                         BOSONO_VARIABLE_LIMIT, random_binary, true,
                         execution) != BOSONOGA_EXIT_SUCCESS)
        return BOSONOGA_EXIT_FAILURE;
      continue;
    }
    if (matches(tokens, position, "_bosonoga_info")) {
      position++;
      if (output->startup_info_count >= 16 || position >= tokens->count ||
          !copy_string(&tokens->items[position],
                       output->startup_info[output->startup_info_count]))
        return syntax_error(tokens, position, "Expected a startup info string");
      output->startup_info_count++;
      position++;
      continue;
    }
    if (matches(tokens, position, "_bosonoga_if")) {
      position++;
      if (position >= tokens->count)
        return syntax_error(tokens, position, "Expected a boolean variable");
      size_t index = find_variable(&tokens->items[position], storage->items,
                                   storage->count);
      if (index == storage->count ||
          storage->items[index].value.kind != BOSONO_VALUE_BOOL)
        return syntax_error(tokens, position, "Expected a boolean variable");
      position++;
      if (!matches(tokens, position++, "_bosonoga_eq"))
        return syntax_error(tokens, position - 1,
                            "Expected eq in boolean condition");
      bool expected = matches(tokens, position, "true");
      if (!expected && !matches(tokens, position, "false"))
        return syntax_error(tokens, position, "Expected true or false");
      position++;
      bool condition = storage->items[index].value.boolean == expected;
      for (size_t branch = 0; branch < 2; branch++) {
        if (!matches(tokens, position, "_bosonoga_di"))
          return syntax_error(tokens, position, "Expected di");
        size_t start = ++position;
        size_t nesting = 1;
        while (position < tokens->count && nesting) {
          if (matches(tokens, position, "_bosonoga_di")) nesting++;
          if (matches(tokens, position, "_bosonoga_do")) nesting--;
          if (nesting) position++;
        }
        if (nesting) return syntax_error(tokens, position, "Expected do");
        if (condition == (branch == 0)) {
          Tokens selected = {.items = tokens->items + start,
                             .count = position - start};
          if (parse_rules(&selected, output, storage, random_binary, depth + 1,
                          execution) != BOSONOGA_EXIT_SUCCESS)
            return BOSONOGA_EXIT_FAILURE;
        }
        position++;
        if (branch == 0 && matches(tokens, position, "_bosonoga_else"))
          position++;
        else
          break;
      }
      continue;
    }
    if (matches(tokens, position, "_bosonoga_asset")) {
      if (parse_asset(tokens, &position, output) != BOSONOGA_EXIT_SUCCESS)
        return BOSONOGA_EXIT_FAILURE;
      continue;
    }
    if (matches(tokens, position, "_bosonoga_theme")) {
      if (parse_theme(tokens, &position, output) != BOSONOGA_EXIT_SUCCESS) {
        return BOSONOGA_EXIT_FAILURE;
      }
      continue;
    }
    if (output->rule_count >= BOSONO_RULE_LIMIT) {
      return syntax_error(
          tokens, position,
          "Too many key blocks (maximum " BOSONO_TEXT(BOSONO_RULE_LIMIT) ")");
    }
    Token const* const trigger = &tokens->items[position];
    bool const is_press = trigger->length == sizeof "_bosonoga_press_" &&
                          memcmp(trigger->text, "_bosonoga_press_",
                                 sizeof "_bosonoga_press_" - 1) == 0;
    bool const is_down = trigger->length == sizeof "_bosonoga_down_" &&
                         memcmp(trigger->text, "_bosonoga_down_",
                                sizeof "_bosonoga_down_" - 1) == 0;
    if (!is_press && !is_down) {
      return syntax_error(
          tokens, position,
          "Expected _bosonoga_press_<key> or _bosonoga_down_<key>");
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
    if (!matches(tokens, position, "_bosonoga_di")) {
      return syntax_error(tokens, position, "Expected di after key trigger");
    }
    position++;
    rule->variable_start = storage->count;
    rule->variable_count = 0;
    size_t variable_count = 0;
    if (parse_statements(tokens, &position, rule, storage, &variable_count,
                         true, 0, execution) != BOSONOGA_EXIT_SUCCESS) {
      return BOSONOGA_EXIT_FAILURE;
    }
    output->rule_count++;
  }
  return BOSONOGA_EXIT_SUCCESS;
}

int parse_with_aliases(Tokens const* tokens, BosonoProgram* output,
                       BosonoVariables* variables, bool random_binary,
                       BosonoAliases const* aliases) {
  BosonoExecution execution = {.aliases = aliases};
  variables->count = 0;
  output->asset_count = 0;
  output->theme_count = 0;
  output->rule_count = 0;
  output->startup_info_count = 0;
  int result =
      parse_rules(tokens, output, variables, random_binary, 0, &execution);
  if (result == BOSONOGA_EXIT_SUCCESS &&
      output->theme_count < BOSONO_THEME_MIN &&
      (output->asset_count != 0 || output->rule_count != 0 ||
       output->startup_info_count == 0)) {
    result = syntax_error(tokens, tokens->count,
                          "Expected at least " BOSONO_TEXT(
                              BOSONO_THEME_MIN) " theme declaration");
  }
  if (result != BOSONOGA_EXIT_SUCCESS) {
    output->asset_count = 0;
    variables->count = 0;
    output->rule_count = 0;
    output->theme_count = 0;
  }
  return result;
}

int parse(Tokens const* tokens, BosonoProgram* output,
          BosonoVariables* variables, bool random_binary) {
  return parse_with_aliases(tokens, output, variables, random_binary, NULL);
}

static int execute_import(char const* filename, BosonoExecution* execution,
                          int32_t* result) {
  if (execution->import_depth >= BOSONO_CONDITION_DEPTH_LIMIT) {
    fprintf(stderr, "Import nesting limit exceeded\n");
    return BOSONOGA_EXIT_FAILURE;
  }
  FILE* file = NULL;
#if defined(_WIN32)
  (void)fopen_s(&file, filename, "rb");
#else
  file = fopen(filename, "rb");
#endif
  if (!file) {
    fprintf(stderr, "Unable to open imported script %s\n", filename);
    return BOSONOGA_EXIT_FAILURE;
  }
  char* source = malloc(BOSONO_LIMIT);
  Token* items = malloc(sizeof(Token) * BOSONO_TOKEN_LIMIT);
  BosonoVariables* storage = malloc(sizeof *storage);
  BosonoRule* rule = calloc(1, sizeof *rule);
  int status = BOSONOGA_EXIT_FAILURE;
  if (!source || !items || !storage || !rule) {
    fprintf(stderr, "Unable to allocate imported script storage\n");
    (void)fclose(file);
    goto cleanup;
  }
  size_t const length = fread(source, 1, BOSONO_LIMIT - 1, file);
  bool const failed = ferror(file) != 0;
  int const extra = fgetc(file);
  bool const read_failed = failed || ferror(file) != 0;
  (void)fclose(file);
  source[length] = '\0';
  if (read_failed || extra != EOF || memchr(source, '\0', length) != NULL) {
    fprintf(stderr, "Invalid imported script %s\n", filename);
    goto cleanup;
  }
  Tokens tokens = {.items = items};
  if (tokenize(source, &tokens) != BOSONOGA_EXIT_SUCCESS ||
      (execution->aliases &&
       apply_aliases(source, &tokens, execution->aliases) !=
           BOSONOGA_EXIT_SUCCESS) ||
      tokens.count >= BOSONO_TOKEN_LIMIT)
    goto cleanup;
  tokens.items[tokens.count++] =
      (Token){.text = "_bosonoga_do", .length = sizeof "_bosonoga_do" - 1};
  storage->count = 0;
  size_t position = 0, variable_count = 0;
  BosonoExecution imported = {.aliases = execution->aliases,
                              .import_depth = execution->import_depth + 1,
                              .imported = true};
  if (parse_statements(&tokens, &position, rule, storage, &variable_count, true,
                       0, &imported) != BOSONOGA_EXIT_SUCCESS)
    goto cleanup;
  if (position != tokens.count || !imported.returned) {
    (void)syntax_error(&tokens, position, "Expected an imported script return");
    goto cleanup;
  }
  for (size_t i = 0; i < rule->info_count; i++)
    (void)printf("%s\n", rule->info[i]);
  *result = imported.result;
  status = BOSONOGA_EXIT_SUCCESS;
cleanup:
  free(rule);
  free(storage);
  free(items);
  free(source);
  return status;
}
