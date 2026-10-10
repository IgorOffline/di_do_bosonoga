#include <float.h>
#include <inttypes.h>

#include "bosonoga.h"

#define BOSONOGA_VARIABLE_NOT_FOUND BOSONOGA_VARIABLE_LIMIT
#define BOSONOGA_PARAM_LIMIT 8

#define BOSONOGA_VAR_TOKEN_COUNT 4
#define BOSONOGA_SET_MIN_TOKEN_COUNT 4
#define BOSONOGA_SHIFT_OR_TOKEN_COUNT 2
#define BOSONOGA_COMPARISON_TOKEN_COUNT 3

#define BOSONOGA_CONVERT_TOKEN_COUNT 5
#define BOSONOGA_CONVERT_SET_TOKEN_COUNT 4

#define BOSONOGA_I32_SUFFIX "_i32"
#define BOSONOGA_F32_SUFFIX "_f32"
#define BOSONOGA_I32_RANGE_LIMIT 2147483648.0f
#define BOSONOGA_F32_TOLERANCE_3 1e-3
#define BOSONOGA_F32_TOLERANCE_6 1e-6
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

static int parse_f32_literal(const char* token, float* result) {
  if (!ends_with(token, BOSONOGA_F32_SUFFIX)) {
    return BOSONOGA_EXIT_FAILURE;
  }
  const BOSONOGA_SIZE length =
      BOSONOGA_STRLEN(token) - BOSONOGA_STRLEN(BOSONOGA_F32_SUFFIX);
  BOSONOGA_SIZE i = token[0] == '-' ? 1 : 0;
  const BOSONOGA_SIZE integer_start = i;
  while (i < length && is_digit(token[i])) {
    i++;
  }
  if (i == integer_start) {
    return BOSONOGA_EXIT_FAILURE;
  }
  if (i < length && token[i] == '.') {
    i++;
    const BOSONOGA_SIZE fraction_start = i;
    while (i < length && is_digit(token[i])) {
      i++;
    }
    if (i == fraction_start) {
      return BOSONOGA_EXIT_FAILURE;
    }
  }
  if (i != length) {
    return BOSONOGA_EXIT_FAILURE;
  }

  char digits[BOSONOGA_TOKEN_LENGTH + 1];
  BOSONOGA_MEMCPY(digits, token, length);
  digits[length] = '\0';
  const double value = BOSONOGA_STRTOD(digits, NULL);
  if (!(value >= -(double)FLT_MAX && value <= (double)FLT_MAX)) {
    return BOSONOGA_EXIT_FAILURE;
  }
  *result = (float)value;
  return BOSONOGA_EXIT_SUCCESS;
}

static int parse_value(const Regina* regina, const char* token,
                       BosonogaValue* result);

static bool is_conversion(const char* token) {
  return strings_equal(token, "to_f32") || strings_equal(token, "to_i32");
}

static int convert_value(const Regina* regina, const char* operation,
                         const char* source_token, BosonogaValue* result) {
  BosonogaValue source;
  if (parse_value(regina, source_token, &source) != BOSONOGA_EXIT_SUCCESS) {
    return BOSONOGA_EXIT_FAILURE;
  }
  if (strings_equal(operation, "to_f32") && source.kind == BOSONOGA_KIND_I32) {
    result->kind = BOSONOGA_KIND_F32;
    result->f32 = (float)source.i32;
    return BOSONOGA_EXIT_SUCCESS;
  }
  if (strings_equal(operation, "to_i32") && source.kind == BOSONOGA_KIND_F32 &&
      source.f32 >= -BOSONOGA_I32_RANGE_LIMIT &&
      source.f32 < BOSONOGA_I32_RANGE_LIMIT) {
    result->kind = BOSONOGA_KIND_I32;
    result->i32 = (int32_t)source.f32;
    return BOSONOGA_EXIT_SUCCESS;
  }
  return BOSONOGA_EXIT_FAILURE;
}

static int parse_flags(const Regina* regina, const char* token,
                       uint32_t* result) {
  const BOSONOGA_SIZE index = index_of_variable(regina, token);
  if (index != BOSONOGA_VARIABLE_NOT_FOUND) {
    const BosonogaVariable* variable = &regina->variables[index];
    if (variable->value.kind != BOSONOGA_KIND_FLAGS) {
      return BOSONOGA_EXIT_FAILURE;
    }
    *result = variable->value.flags;
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

static int parse_value(const Regina* regina, const char* token,
                       BosonogaValue* result) {
  const BOSONOGA_SIZE index = index_of_variable(regina, token);
  if (index != BOSONOGA_VARIABLE_NOT_FOUND) {
    *result = regina->variables[index].value;
    return BOSONOGA_EXIT_SUCCESS;
  }
  if (parse_i32_literal(token, &result->i32) == BOSONOGA_EXIT_SUCCESS) {
    result->kind = BOSONOGA_KIND_I32;
    return BOSONOGA_EXIT_SUCCESS;
  }
  if (parse_f32_literal(token, &result->f32) == BOSONOGA_EXIT_SUCCESS) {
    result->kind = BOSONOGA_KIND_F32;
    return BOSONOGA_EXIT_SUCCESS;
  }
  if (parse_flags(regina, token, &result->flags) == BOSONOGA_EXIT_SUCCESS) {
    result->kind = BOSONOGA_KIND_FLAGS;
    return BOSONOGA_EXIT_SUCCESS;
  }
  return BOSONOGA_EXIT_FAILURE;
}

static bool f32_close(float left, float right, double tolerance) {
  const double a = (double)left;
  const double b = (double)right;
  const double difference = a > b ? a - b : b - a;
  const double size_a = a < 0.0 ? -a : a;
  const double size_b = b < 0.0 ? -b : b;
  const double scale = size_a > size_b ? size_a : size_b;
  return difference <= tolerance * scale || difference <= tolerance;
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
         strings_equal(token, "if") || strings_equal(token, "else") ||
         starts_with(token, "to_") || strings_equal(token, "di") ||
         strings_equal(token, "do") || strings_equal(token, "return") ||
         strings_equal(token, "nihil");
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
    variable->value.kind = BOSONOGA_KIND_FLAGS;
    variable->value.flags = flags;
    *position = next;
    return BOSONOGA_EXIT_SUCCESS;
  }

  BosonogaValue initial;
  BOSONOGA_SIZE consumed = BOSONOGA_VAR_TOKEN_COUNT;
  if (is_conversion(value)) {
    if (tokens_left(regina, *position) < BOSONOGA_CONVERT_TOKEN_COUNT ||
        convert_value(regina, value, token_at(regina, *position + 4),
                      &initial) != BOSONOGA_EXIT_SUCCESS) {
      return BOSONOGA_EXIT_FAILURE;
    }
    consumed = BOSONOGA_CONVERT_TOKEN_COUNT;
  } else if (parse_i32_literal(value, &initial.i32) == BOSONOGA_EXIT_SUCCESS) {
    initial.kind = BOSONOGA_KIND_I32;
  } else if (parse_f32_literal(value, &initial.f32) == BOSONOGA_EXIT_SUCCESS) {
    initial.kind = BOSONOGA_KIND_F32;
  } else {
    return BOSONOGA_EXIT_FAILURE;
  }

  BosonogaVariable* variable = add_variable(regina, name);
  variable->value = initial;
  *position += consumed;
  return BOSONOGA_EXIT_SUCCESS;
}

static int run_set(Regina* regina, BOSONOGA_SIZE* position) {
  if (tokens_left(regina, *position) < BOSONOGA_SET_MIN_TOKEN_COUNT) {
    return BOSONOGA_EXIT_FAILURE;
  }

  const char* operation = token_at(regina, *position + 2);
  const BOSONOGA_SIZE target =
      index_of_variable(regina, token_at(regina, *position + 1));
  if (target == BOSONOGA_VARIABLE_NOT_FOUND) {
    return BOSONOGA_EXIT_FAILURE;
  }
  BosonogaValue* destination = &regina->variables[target].value;

  if (is_conversion(operation)) {
    const BOSONOGA_SIZE next = *position + BOSONOGA_CONVERT_SET_TOKEN_COUNT;
    BosonogaValue converted;
    if (convert_value(regina, operation, token_at(regina, *position + 3),
                      &converted) != BOSONOGA_EXIT_SUCCESS ||
        converted.kind != destination->kind ||
        (next < regina->token_count &&
         !is_statement_keyword(token_at(regina, next)))) {
      return BOSONOGA_EXIT_FAILURE;
    }
    *destination = converted;
    *position = next;
    return BOSONOGA_EXIT_SUCCESS;
  }

  const bool subtract = strings_equal(operation, "to_subtract");
  if ((!subtract && !strings_equal(operation, "to_add")) ||
      destination->kind == BOSONOGA_KIND_FLAGS) {
    return BOSONOGA_EXIT_FAILURE;
  }
  const bool is_f32 = destination->kind == BOSONOGA_KIND_F32;

  BOSONOGA_SIZE next = *position + 3;
  BOSONOGA_SIZE param_count = 0;
  int64_t whole = 0;
  double real = 0.0;
  while (next < regina->token_count &&
         !is_statement_keyword(token_at(regina, next))) {
    BosonogaValue value;
    if (param_count == BOSONOGA_PARAM_LIMIT ||
        parse_value(regina, token_at(regina, next), &value) !=
            BOSONOGA_EXIT_SUCCESS ||
        value.kind != destination->kind) {
      return BOSONOGA_EXIT_FAILURE;
    }
    const bool minus = subtract && param_count > 0;
    if (is_f32) {
      real += minus ? -(double)value.f32 : (double)value.f32;
    } else {
      whole += minus ? -(int64_t)value.i32 : (int64_t)value.i32;
    }
    param_count++;
    next++;
  }

  if (subtract && param_count == 1) {
    whole = -whole;
    real = -real;
  }
  if (param_count == 0) {
    return BOSONOGA_EXIT_FAILURE;
  }
  if (is_f32) {
    if (!(real >= -(double)FLT_MAX && real <= (double)FLT_MAX)) {
      return BOSONOGA_EXIT_FAILURE;
    }
    destination->f32 = (float)real;
  } else {
    if (whole < INT32_MIN || whole > INT32_MAX) {
      return BOSONOGA_EXIT_FAILURE;
    }
    destination->i32 = (int32_t)whole;
  }
  *position = next;
  return BOSONOGA_EXIT_SUCCESS;
}

static int evaluate_comparison(const Regina* regina, BOSONOGA_SIZE position,
                               bool* comparison) {
  const char* left_token = token_at(regina, position);
  const char* operation = token_at(regina, position + 1);
  const char* right_token = token_at(regina, position + 2);

  if (strings_equal(operation, "shift_has")) {
    uint32_t left;
    uint32_t right;
    if (parse_flags(regina, left_token, &left) != BOSONOGA_EXIT_SUCCESS ||
        parse_flags(regina, right_token, &right) != BOSONOGA_EXIT_SUCCESS) {
      return BOSONOGA_EXIT_FAILURE;
    }
    *comparison = (left & right) == right;
    return BOSONOGA_EXIT_SUCCESS;
  }

  BosonogaValue left;
  BosonogaValue right;
  if (parse_value(regina, left_token, &left) != BOSONOGA_EXIT_SUCCESS ||
      parse_value(regina, right_token, &right) != BOSONOGA_EXIT_SUCCESS ||
      left.kind != right.kind || left.kind == BOSONOGA_KIND_FLAGS) {
    return BOSONOGA_EXIT_FAILURE;
  }

  if (left.kind == BOSONOGA_KIND_F32) {
    if (strings_equal(operation, "gt")) {
      *comparison = left.f32 > right.f32;
    } else if (strings_equal(operation, "lt")) {
      *comparison = left.f32 < right.f32;
    } else if (strings_equal(operation, "eq_3")) {
      *comparison = f32_close(left.f32, right.f32, BOSONOGA_F32_TOLERANCE_3);
    } else if (strings_equal(operation, "eq_6")) {
      *comparison = f32_close(left.f32, right.f32, BOSONOGA_F32_TOLERANCE_6);
    } else if (strings_equal(operation, "not_eq_3")) {
      *comparison = !f32_close(left.f32, right.f32, BOSONOGA_F32_TOLERANCE_3);
    } else if (strings_equal(operation, "not_eq_6")) {
      *comparison = !f32_close(left.f32, right.f32, BOSONOGA_F32_TOLERANCE_6);
    } else {
      return BOSONOGA_EXIT_FAILURE;
    }
    return BOSONOGA_EXIT_SUCCESS;
  }

  if (strings_equal(operation, "gt")) {
    *comparison = left.i32 > right.i32;
  } else if (strings_equal(operation, "lt")) {
    *comparison = left.i32 < right.i32;
  } else if (strings_equal(operation, "eq")) {
    *comparison = left.i32 == right.i32;
  } else if (strings_equal(operation, "not_eq")) {
    *comparison = left.i32 != right.i32;
  } else {
    return BOSONOGA_EXIT_FAILURE;
  }
  return BOSONOGA_EXIT_SUCCESS;
}

static int find_block_end(const Regina* regina, BOSONOGA_SIZE start,
                          BOSONOGA_SIZE end, BOSONOGA_SIZE* closing) {
  BOSONOGA_SIZE depth = 1;
  for (BOSONOGA_SIZE position = start; position < end; position++) {
    const char* token = token_at(regina, position);
    if (strings_equal(token, "di")) {
      depth++;
    } else if (strings_equal(token, "do")) {
      depth--;
      if (depth == 0) {
        if (position == start) {
          return BOSONOGA_EXIT_FAILURE;
        }
        *closing = position;
        return BOSONOGA_EXIT_SUCCESS;
      }
    }
  }
  return BOSONOGA_EXIT_FAILURE;
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

static int evaluate_condition(const Regina* regina, BOSONOGA_SIZE* position,
                              BOSONOGA_SIZE end, bool* result) {
  BOSONOGA_SIZE next = *position;
  bool condition = false;
  bool and_group = true;
  while (true) {
    if (end - next < BOSONOGA_COMPARISON_TOKEN_COUNT) {
      return BOSONOGA_EXIT_FAILURE;
    }
    bool comparison = false;
    if (evaluate_comparison(regina, next, &comparison) !=
        BOSONOGA_EXIT_SUCCESS) {
      return BOSONOGA_EXIT_FAILURE;
    }
    and_group = and_group && comparison;
    next += BOSONOGA_COMPARISON_TOKEN_COUNT;
    if (next == end) {
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
  *result = condition || and_group;
  *position = next;
  return BOSONOGA_EXIT_SUCCESS;
}

static int run_if(Regina* regina, BOSONOGA_SIZE* position, BOSONOGA_SIZE end,
                  bool* returned) {
  BOSONOGA_SIZE next = *position + 1;
  bool has_condition = true;
  bool chosen = false;
  BOSONOGA_SIZE chosen_start = 0;
  BOSONOGA_SIZE chosen_closing = 0;
  while (true) {
    bool condition = true;
    if (has_condition && evaluate_condition(regina, &next, end, &condition) !=
                             BOSONOGA_EXIT_SUCCESS) {
      return BOSONOGA_EXIT_FAILURE;
    }
    if (next >= end || !strings_equal(token_at(regina, next), "di")) {
      return BOSONOGA_EXIT_FAILURE;
    }
    const BOSONOGA_SIZE start = next + 1;
    BOSONOGA_SIZE closing;
    if (find_block_end(regina, start, end, &closing) != BOSONOGA_EXIT_SUCCESS) {
      return BOSONOGA_EXIT_FAILURE;
    }
    if (!chosen && condition) {
      chosen = true;
      chosen_start = start;
      chosen_closing = closing;
    }
    next = closing + 1;

    if (!has_condition || next == end ||
        !strings_equal(token_at(regina, next), "else")) {
      break;
    }
    next++;
    has_condition = next < end && strings_equal(token_at(regina, next), "if");
    if (has_condition) {
      next++;
    }
  }

  int status = BOSONOGA_EXIT_SUCCESS;
  if (chosen) {
    const BOSONOGA_SIZE saved_count = regina->variable_count;
    status = run_body(regina, chosen_start, chosen_closing, returned);
    regina->variable_count = saved_count;
  }
  *position = next;
  return status;
}

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
  BOSONOGA_SIZE closing;
  if (find_block_end(regina, start, end, &closing) != BOSONOGA_EXIT_SUCCESS) {
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
    iterator->value.kind = BOSONOGA_KIND_I32;
  }
  const BOSONOGA_SIZE body_count = regina->variable_count;
  int status = BOSONOGA_EXIT_SUCCESS;
  for (int32_t i = 0; i < limit; i++) {
    if (iterator != NULL) {
      iterator->value.i32 = i;
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
      status = run_if(regina, &position, end, returned);
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
    switch (variable->value.kind) {
      case BOSONOGA_KIND_FLAGS:
        BOSONOGA_PRINTF("variables[%zu]: %s = flags %" PRIu32 "\n", i,
                        variable->name, variable->value.flags);
        break;
      case BOSONOGA_KIND_F32:
        BOSONOGA_PRINTF("variables[%zu]: %s = %.6g_f32\n", i, variable->name,
                        (double)variable->value.f32);
        break;
      case BOSONOGA_KIND_I32:
        BOSONOGA_PRINTF("variables[%zu]: %s = %" PRId32 "_i32\n", i,
                        variable->name, variable->value.i32);
        break;
    }
  }
  BOSONOGA_PRINTF(BOSONOGA_LOG_SEPARATOR);
}

static void reset_regina(Regina* regina) {
  regina->line_count = 0;
  regina->token_count = 0;
  regina->variable_count = 0;
  regina->version_count = 0;
  regina->lega_count = 0;
  regina->aka_count = 0;
  regina->main_count = 0;
}

int bosonoga_core(Regina* regina, const char* header, const char* input) {
  reset_regina(regina);
  int status = bosonoga_load_program(regina, header, input);
  if (status == BOSONOGA_EXIT_SUCCESS) {
    status = run_main(regina);
  }
  if (BOSONOGA_LOG_PARSETOK) {
    log_regina(regina);
  }
  return status;
}
