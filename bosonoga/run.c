#include <inttypes.h>

#include "bosonoga.h"

#define BOSONOGA_VARIABLE_NOT_FOUND BOSONOGA_VARIABLE_LIMIT
#define BOSONOGA_PARAM_LIMIT 8

#define BOSONOGA_VAR_TOKEN_COUNT 4
#define BOSONOGA_SET_MIN_TOKEN_COUNT 4
#define BOSONOGA_SHIFT_OR_TOKEN_COUNT 2
#define BOSONOGA_COMPARISON_TOKEN_COUNT 3
#define BOSONOGA_BRANCHES_TOKEN_COUNT 6

#define BOSONOGA_I32_SUFFIX "_i32"
#define BOSONOGA_SHIFT_PREFIX "shift_"
#define BOSONOGA_SHIFT_DIGIT_LIMIT 2
#define BOSONOGA_SHIFT_BIT_LIMIT 32

#define BOSONOGA_LOG_PARSETOK false
#define BOSONOGA_LOG_SEPARATOR "=== === === === ===\n"

static bool starts_with(const char* text, const char* prefix) {
  return BOSONOGA_STRNCMP(text, prefix, BOSONOGA_STRLEN(prefix)) ==
         BOSONOGA_STRINGS_EQUAL;
}

static bool ends_with(const char* text, const char* suffix) {
  const BOSONOGA_SIZE text_length = BOSONOGA_STRLEN(text);
  const BOSONOGA_SIZE suffix_length = BOSONOGA_STRLEN(suffix);
  return text_length > suffix_length &&
         strings_equal(text + text_length - suffix_length, suffix);
}

static bool is_digit(char character) {
  return character >= '0' && character <= '9';
}

static BOSONOGA_SIZE index_of_variable(const Regina* regina, const char* name) {
  for (BOSONOGA_SIZE i = 0; i < regina->variable_count; i++) {
    if (strings_equal(regina->variables[i].name, name)) {
      return i;
    }
  }
  return BOSONOGA_VARIABLE_NOT_FOUND;
}

static BosonogaVariable* add_variable(Regina* regina, const char* name) {
  BosonogaVariable* variable = &regina->variables[regina->variable_count++];
  copy_text(variable->name, name);
  return variable;
}

static int parse_i32(const char* text, BOSONOGA_SIZE length, int32_t* result) {
  const char* end = text + length;
  const bool negative = length > 0 && *text == '-';
  if (negative) {
    text++;
  }
  if (text == end) {
    return BOSONOGA_EXIT_FAILURE;
  }

  const int64_t limit = negative ? -(int64_t)INT32_MIN : INT32_MAX;
  int64_t value = 0;
  for (; text != end; text++) {
    if (!is_digit(*text)) {
      return BOSONOGA_EXIT_FAILURE;
    }
    value = value * 10 + (*text - '0');
    if (value > limit) {
      return BOSONOGA_EXIT_FAILURE;
    }
  }
  *result = (int32_t)(negative ? -value : value);
  return BOSONOGA_EXIT_SUCCESS;
}

static int parse_i32_literal(const char* token, int32_t* result) {
  if (!ends_with(token, BOSONOGA_I32_SUFFIX)) {
    return BOSONOGA_EXIT_FAILURE;
  }
  return parse_i32(
      token, BOSONOGA_STRLEN(token) - BOSONOGA_STRLEN(BOSONOGA_I32_SUFFIX),
      result);
}

static int parse_operand(const Regina* regina, const char* token,
                         int32_t* result) {
  const BOSONOGA_SIZE index = index_of_variable(regina, token);
  if (index != BOSONOGA_VARIABLE_NOT_FOUND) {
    const BosonogaVariable* variable = &regina->variables[index];
    if (variable->is_flags) {
      return BOSONOGA_EXIT_FAILURE;
    }
    *result = variable->value;
    return BOSONOGA_EXIT_SUCCESS;
  }

  BOSONOGA_SIZE length = BOSONOGA_STRLEN(token);
  if (ends_with(token, BOSONOGA_I32_SUFFIX)) {
    length -= BOSONOGA_STRLEN(BOSONOGA_I32_SUFFIX);
  }
  return parse_i32(token, length, result);
}

static int parse_flags(const Regina* regina, const char* token,
                       uint32_t* result) {
  const BOSONOGA_SIZE index = index_of_variable(regina, token);
  if (index != BOSONOGA_VARIABLE_NOT_FOUND) {
    const BosonogaVariable* variable = &regina->variables[index];
    if (!variable->is_flags) {
      return BOSONOGA_EXIT_FAILURE;
    }
    *result = variable->flags;
    return BOSONOGA_EXIT_SUCCESS;
  }

  if (!starts_with(token, BOSONOGA_SHIFT_PREFIX)) {
    return BOSONOGA_EXIT_FAILURE;
  }
  const char* digits = token + BOSONOGA_STRLEN(BOSONOGA_SHIFT_PREFIX);
  const BOSONOGA_SIZE digit_count = BOSONOGA_STRLEN(digits);
  if (digit_count == 0 || digit_count > BOSONOGA_SHIFT_DIGIT_LIMIT) {
    return BOSONOGA_EXIT_FAILURE;
  }

  uint32_t bit = 0;
  for (BOSONOGA_SIZE i = 0; i < digit_count; i++) {
    if (!is_digit(digits[i])) {
      return BOSONOGA_EXIT_FAILURE;
    }
    bit = bit * 10U + (uint32_t)(digits[i] - '0');
  }
  if (bit >= BOSONOGA_SHIFT_BIT_LIMIT) {
    return BOSONOGA_EXIT_FAILURE;
  }
  *result = UINT32_C(1) << bit;
  return BOSONOGA_EXIT_SUCCESS;
}

static int parse_return_status(const char* keyword, int* status) {
  if (strings_equal(keyword, "exit_success")) {
    *status = BOSONOGA_EXIT_SUCCESS;
    return BOSONOGA_EXIT_SUCCESS;
  }
  if (strings_equal(keyword, "exit_failure")) {
    *status = BOSONOGA_EXIT_FAILURE;
    return BOSONOGA_EXIT_SUCCESS;
  }
  return BOSONOGA_EXIT_FAILURE;
}

static bool is_statement_keyword(const char* token) {
  return strings_equal(token, "var") || strings_equal(token, "set") ||
         strings_equal(token, "if") || starts_with(token, "to_") ||
         strings_equal(token, "di") || strings_equal(token, "do") ||
         strings_equal(token, "return") || strings_equal(token, "nihil");
}

static int run_var(Regina* regina, BOSONOGA_SIZE* position) {
  if (tokens_left(regina, *position) < BOSONOGA_VAR_TOKEN_COUNT ||
      !strings_equal(token_at(regina, *position + 2), "is")) {
    return BOSONOGA_EXIT_FAILURE;
  }

  const char* name = token_at(regina, *position + 1);
  const char* value = token_at(regina, *position + 3);
  if (index_of_variable(regina, name) != BOSONOGA_VARIABLE_NOT_FOUND ||
      regina->variable_count == BOSONOGA_VARIABLE_LIMIT) {
    return BOSONOGA_EXIT_FAILURE;
  }

  uint32_t flags;
  if (parse_flags(regina, value, &flags) == BOSONOGA_EXIT_SUCCESS) {
    BOSONOGA_SIZE next = *position + BOSONOGA_VAR_TOKEN_COUNT;
    while (next < regina->token_count &&
           strings_equal(token_at(regina, next), "shift_or")) {
      uint32_t operand;
      if (tokens_left(regina, next) < BOSONOGA_SHIFT_OR_TOKEN_COUNT ||
          parse_flags(regina, token_at(regina, next + 1), &operand) !=
              BOSONOGA_EXIT_SUCCESS) {
        return BOSONOGA_EXIT_FAILURE;
      }
      flags |= operand;
      next += BOSONOGA_SHIFT_OR_TOKEN_COUNT;
    }

    BosonogaVariable* variable = add_variable(regina, name);
    variable->is_flags = true;
    variable->flags = flags;
    *position = next;
    return BOSONOGA_EXIT_SUCCESS;
  }

  int32_t number;
  if (parse_i32_literal(value, &number) != BOSONOGA_EXIT_SUCCESS) {
    return BOSONOGA_EXIT_FAILURE;
  }

  BosonogaVariable* variable = add_variable(regina, name);
  variable->is_flags = false;
  variable->value = number;
  *position += BOSONOGA_VAR_TOKEN_COUNT;
  return BOSONOGA_EXIT_SUCCESS;
}

static int run_set(Regina* regina, BOSONOGA_SIZE* position) {
  if (tokens_left(regina, *position) < BOSONOGA_SET_MIN_TOKEN_COUNT) {
    return BOSONOGA_EXIT_FAILURE;
  }

  const char* operation = token_at(regina, *position + 2);
  const bool subtract = strings_equal(operation, "to_subtract");
  if (!subtract && !strings_equal(operation, "to_add")) {
    return BOSONOGA_EXIT_FAILURE;
  }

  const BOSONOGA_SIZE target =
      index_of_variable(regina, token_at(regina, *position + 1));
  if (target == BOSONOGA_VARIABLE_NOT_FOUND ||
      regina->variables[target].is_flags) {
    return BOSONOGA_EXIT_FAILURE;
  }

  BOSONOGA_SIZE next = *position + 3;
  BOSONOGA_SIZE param_count = 0;
  int64_t result = 0;
  while (next < regina->token_count &&
         !is_statement_keyword(token_at(regina, next))) {
    int32_t value;
    if (param_count == BOSONOGA_PARAM_LIMIT ||
        parse_operand(regina, token_at(regina, next), &value) !=
            BOSONOGA_EXIT_SUCCESS) {
      return BOSONOGA_EXIT_FAILURE;
    }
    if (subtract && param_count > 0) {
      result -= value;
    } else {
      result += value;
    }
    param_count++;
    next++;
  }

  if (subtract && param_count == 1) {
    result = -result;
  }
  if (param_count == 0 || result < INT32_MIN || result > INT32_MAX) {
    return BOSONOGA_EXIT_FAILURE;
  }

  regina->variables[target].value = (int32_t)result;
  *position = next;
  return BOSONOGA_EXIT_SUCCESS;
}

static int evaluate_comparison(const Regina* regina, BOSONOGA_SIZE position,
                               bool* comparison) {
  const char* left_token = token_at(regina, position);
  const char* operation = token_at(regina, position + 1);
  const char* right_token = token_at(regina, position + 2);

  if (strings_equal(operation, "shift_eq")) {
    uint32_t left;
    uint32_t right;
    if (parse_flags(regina, left_token, &left) != BOSONOGA_EXIT_SUCCESS ||
        parse_flags(regina, right_token, &right) != BOSONOGA_EXIT_SUCCESS) {
      return BOSONOGA_EXIT_FAILURE;
    }
    *comparison = (left & right) == right;
    return BOSONOGA_EXIT_SUCCESS;
  }

  int32_t left;
  int32_t right;
  if (parse_operand(regina, left_token, &left) != BOSONOGA_EXIT_SUCCESS ||
      parse_operand(regina, right_token, &right) != BOSONOGA_EXIT_SUCCESS) {
    return BOSONOGA_EXIT_FAILURE;
  }

  if (strings_equal(operation, "gt")) {
    *comparison = left > right;
  } else if (strings_equal(operation, "lt")) {
    *comparison = left < right;
  } else if (strings_equal(operation, "eq")) {
    *comparison = left == right;
  } else if (strings_equal(operation, "not_eq")) {
    *comparison = left != right;
  } else {
    return BOSONOGA_EXIT_FAILURE;
  }
  return BOSONOGA_EXIT_SUCCESS;
}

static int run_branches(const Regina* regina, BOSONOGA_SIZE position,
                        bool condition) {
  if (tokens_left(regina, position) != BOSONOGA_BRANCHES_TOKEN_COUNT ||
      !strings_equal(token_at(regina, position), "di") ||
      !strings_equal(token_at(regina, position + 1), "return") ||
      !strings_equal(token_at(regina, position + 3), "do") ||
      !strings_equal(token_at(regina, position + 4), "return")) {
    return BOSONOGA_EXIT_FAILURE;
  }

  int di_status;
  int do_status;
  if (parse_return_status(token_at(regina, position + 2), &di_status) !=
          BOSONOGA_EXIT_SUCCESS ||
      parse_return_status(token_at(regina, position + 5), &do_status) !=
          BOSONOGA_EXIT_SUCCESS) {
    return BOSONOGA_EXIT_FAILURE;
  }
  return condition ? di_status : do_status;
}

static int run_if(const Regina* regina, BOSONOGA_SIZE position) {
  BOSONOGA_SIZE next = position + 1;
  bool condition = false;
  bool and_group = true;
  while (true) {
    if (tokens_left(regina, next) < BOSONOGA_COMPARISON_TOKEN_COUNT) {
      return BOSONOGA_EXIT_FAILURE;
    }
    bool comparison = false;
    if (evaluate_comparison(regina, next, &comparison) !=
        BOSONOGA_EXIT_SUCCESS) {
      return BOSONOGA_EXIT_FAILURE;
    }
    and_group = and_group && comparison;
    next += BOSONOGA_COMPARISON_TOKEN_COUNT;
    if (next == regina->token_count) {
      return BOSONOGA_EXIT_FAILURE;
    }

    const char* connector = token_at(regina, next);
    if (strings_equal(connector, "or")) {
      condition = condition || and_group;
      and_group = true;
    } else if (!strings_equal(connector, "and")) {
      break;
    }
    next++;
  }
  condition = condition || and_group;
  return run_branches(regina, next, condition);
}

static BOSONOGA_SIZE position_after_main(const Regina* regina) {
  for (BOSONOGA_SIZE position = 0; position < regina->token_count; position++) {
    if (strings_equal(token_at(regina, position), "main")) {
      return position + 1;
    }
  }
  return regina->token_count;
}

static int run_body(Regina* regina, BOSONOGA_SIZE position, BOSONOGA_SIZE end,
                    bool* returned);

static int run_loop(Regina* regina, BOSONOGA_SIZE* position, BOSONOGA_SIZE end,
                    bool* returned) {
  const char* keyword = token_at(regina, *position);
  int32_t limit;
  if (end - *position < 4 || !is_digit(keyword[3]) ||
      parse_i32(keyword + 3, BOSONOGA_STRLEN(keyword + 3), &limit) !=
          BOSONOGA_EXIT_SUCCESS) {
    return BOSONOGA_EXIT_FAILURE;
  }
  BOSONOGA_SIZE opening = *position + 1;
  const char* name = NULL;
  if (strings_equal(token_at(regina, opening), "with")) {
    if (end - *position < 6) {
      return BOSONOGA_EXIT_FAILURE;
    }
    name = token_at(regina, opening + 1);
    opening += 2;
  }
  if (!strings_equal(token_at(regina, opening), "di")) {
    return BOSONOGA_EXIT_FAILURE;
  }
  const BOSONOGA_SIZE start = opening + 1;
  BOSONOGA_SIZE closing = start;
  BOSONOGA_SIZE depth = 1;
  for (; closing < end; closing++) {
    const char* token = token_at(regina, closing);
    if (strings_equal(token, "di")) {
      depth++;
    } else if (strings_equal(token, "do")) {
      depth--;
      if (depth == 0) {
        break;
      }
    }
  }
  if (closing == end || closing == start) {
    return BOSONOGA_EXIT_FAILURE;
  }
  const BOSONOGA_SIZE saved_count = regina->variable_count;
  BosonogaVariable* iterator = NULL;
  if (name != NULL) {
    if (index_of_variable(regina, name) != BOSONOGA_VARIABLE_NOT_FOUND ||
        regina->variable_count == BOSONOGA_VARIABLE_LIMIT ||
        is_statement_keyword(name) || strings_equal(name, "with")) {
      return BOSONOGA_EXIT_FAILURE;
    }
    iterator = add_variable(regina, name);
    iterator->is_flags = false;
  }
  const BOSONOGA_SIZE body_count = regina->variable_count;
  int status = BOSONOGA_EXIT_SUCCESS;
  for (int32_t i = 0; i < limit; i++) {
    if (iterator != NULL) {
      iterator->value = i;
    }
    status = run_body(regina, start, closing, returned);
    regina->variable_count = body_count;
    if (status != BOSONOGA_EXIT_SUCCESS || *returned) {
      break;
    }
  }
  regina->variable_count = saved_count;
  *position = closing + 1;
  return status;
}

static int run_body(Regina* regina, BOSONOGA_SIZE position, BOSONOGA_SIZE end,
                    bool* returned) {
  while (position < end) {
    const char* keyword = token_at(regina, position);
    int status;
    if (strings_equal(keyword, "var")) {
      status = run_var(regina, &position);
    } else if (strings_equal(keyword, "set")) {
      status = run_set(regina, &position);
    } else if (strings_equal(keyword, "if")) {
      const BOSONOGA_SIZE saved_end = regina->token_count;
      regina->token_count = end;
      status = run_if(regina, position);
      regina->token_count = saved_end;
      *returned = true;
      return status;
    } else if (starts_with(keyword, "to_")) {
      status = run_loop(regina, &position, end, returned);
    } else if (strings_equal(keyword, "nihil")) {
      position++;
      status = BOSONOGA_EXIT_SUCCESS;
    } else if (strings_equal(keyword, "return")) {
      if (position + 1 >= end ||
          parse_return_status(token_at(regina, position + 1), &status) !=
              BOSONOGA_EXIT_SUCCESS) {
        return BOSONOGA_EXIT_FAILURE;
      }
      *returned = true;
      return status;
    } else {
      return BOSONOGA_EXIT_FAILURE;
    }
    if (status != BOSONOGA_EXIT_SUCCESS || *returned) {
      return status;
    }
    if (position > end) {
      return BOSONOGA_EXIT_FAILURE;
    }
  }
  return BOSONOGA_EXIT_SUCCESS;
}

static int run_main(Regina* regina) {
  bool returned = false;
  const int status = run_body(regina, position_after_main(regina),
                              regina->token_count, &returned);
  return returned ? status : BOSONOGA_EXIT_FAILURE;
}

static void log_token_summary(const Regina* regina) {
  for (BOSONOGA_SIZE position = 0; position < regina->token_count; position++) {
    const char* token = token_at(regina, position);
    if (strings_equal(token, "version")) {
      BOSONOGA_PRINTF("[V]\n");
      position += BOSONOGA_VERSION_TOKEN_COUNT - 1;
    } else if (strings_equal(token, "lega")) {
      BOSONOGA_PRINTF("[L]\n");
      position += BOSONOGA_LEGA_TOKEN_COUNT - 1;
    } else if (strings_equal(token, "aka")) {
      BOSONOGA_PRINTF("[A]\n");
      position += BOSONOGA_AKA_TOKEN_COUNT - 1;
    } else if (strings_equal(token, "main")) {
      BOSONOGA_PRINTF("[M]\n");
    } else {
      BOSONOGA_PRINTF("[%s]\n", token);
    }
  }
}

static void log_regina(const Regina* regina) {
  BOSONOGA_PRINTF(BOSONOGA_LOG_SEPARATOR);
  BOSONOGA_PRINTF("Regina allocated: %zu bytes\n", sizeof(Regina));
  BOSONOGA_PRINTF(BOSONOGA_LOG_SEPARATOR);
  log_token_summary(regina);
  BOSONOGA_PRINTF(BOSONOGA_LOG_SEPARATOR);

  for (BOSONOGA_SIZE i = 0; i < regina->version_count; i++) {
    const BosonogaVersion* version = &regina->version[i];
    BOSONOGA_PRINTF("version[%zu]: %s (len=%zu)\n", i, version->string,
                    version->len);
  }

  for (BOSONOGA_SIZE i = 0; i < regina->lega_count; i++) {
    const BosonogaLega* lega = &regina->lega[i];
    BOSONOGA_PRINTF("lega[%zu]: %s (len=%zu)\n", i, lega->legal, lega->len);
  }

  for (BOSONOGA_SIZE i = 0; i < regina->aka_count; i++) {
    const BosonogaAka* aka = &regina->aka[i];
    BOSONOGA_PRINTF("aka[%zu].original: %s (len=%zu)\n", i, aka->original,
                    aka->original_len);
    BOSONOGA_PRINTF("aka[%zu].replacement: %s (len=%zu)\n", i, aka->replacement,
                    aka->replacement_len);
  }

  BOSONOGA_PRINTF("main_count: %zu\n", regina->main_count);

  for (BOSONOGA_SIZE i = 0; i < regina->line_count; i++) {
    if (regina->lines[i].data[0] != '\0') {
      BOSONOGA_PRINTF("lines[%zu]: %s\n", i, regina->lines[i].data);
    }
  }

  for (BOSONOGA_SIZE i = 0; i < regina->token_count; i++) {
    BOSONOGA_PRINTF("tokens[%zu]: %s\n", i, token_at(regina, i));
  }

  for (BOSONOGA_SIZE i = 0; i < regina->variable_count; i++) {
    const BosonogaVariable* variable = &regina->variables[i];
    if (variable->is_flags) {
      BOSONOGA_PRINTF("variables[%zu]: %s = flags %" PRIu32 "\n", i,
                      variable->name, variable->flags);
    } else {
      BOSONOGA_PRINTF("variables[%zu]: %s = %" PRId32 "\n", i, variable->name,
                      variable->value);
    }
  }
  BOSONOGA_PRINTF(BOSONOGA_LOG_SEPARATOR);
}

int bosonoga_core(const char* header, const char* input) {
  Regina* regina = BOSONOGA_CALLOC(1, sizeof(Regina));
  if (regina == NULL) {
    return BOSONOGA_EXIT_FAILURE;
  }

  int status = bosonoga_load_program(regina, header, input);
  if (status == BOSONOGA_EXIT_SUCCESS) {
    status = run_main(regina);
  }
  if (BOSONOGA_LOG_PARSETOK) {
    log_regina(regina);
  }

  BOSONOGA_FREE(regina);
  return status;
}
