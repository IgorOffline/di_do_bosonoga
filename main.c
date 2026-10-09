#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOSONOGA_VERSION_LIMIT 1
#define BOSONOGA_LEGA_LIMIT 16
#define BOSONOGA_AKA_LIMIT 16
#define BOSONOGA_MAIN_LIMIT 1
#define BOSONOGA_VARIABLE_LIMIT 64
#define BOSONOGA_VARIABLE_NOT_FOUND BOSONOGA_VARIABLE_LIMIT
#define BOSONOGA_PARAM_LIMIT 8
#define BOSONOGA_SOURCE_LENGTH 8191
#define BOSONOGA_LINE_LIMIT 256
#define BOSONOGA_LINE_LENGTH 255
#define BOSONOGA_TOKEN_PER_LINE_LIMIT 16
#define BOSONOGA_TOKEN_LENGTH 63
#define BOSONOGA_TOKEN_LIMIT \
  (BOSONOGA_LINE_LIMIT * BOSONOGA_TOKEN_PER_LINE_LIMIT)

#define BOSONOGA_VERSION_TOKEN_COUNT 2
#define BOSONOGA_LEGA_TOKEN_COUNT 2
#define BOSONOGA_AKA_TOKEN_COUNT 3
#define BOSONOGA_VAR_TOKEN_COUNT 4
#define BOSONOGA_SET_MIN_TOKEN_COUNT 4
#define BOSONOGA_SHIFT_OR_TOKEN_COUNT 2
#define BOSONOGA_COMPARISON_TOKEN_COUNT 3
#define BOSONOGA_BRANCHES_TOKEN_COUNT 6

#define BOSONOGA_I32_SUFFIX "_i32"
#define BOSONOGA_SHIFT_PREFIX "shift_"
#define BOSONOGA_SHIFT_DIGIT_LIMIT 2
#define BOSONOGA_SHIFT_BIT_LIMIT 32

#define BOSONOGA_EXIT_SUCCESS 0
#define BOSONOGA_EXIT_FAILURE 1
#define BOSONOGA_STRINGS_EQUAL 0
#define BOSONOGA_IO_SUCCESS 0

#define BOSONOGA_LOG_PARSETOK false
#define BOSONOGA_LOG_SEPARATOR "=== === === === ===\n"
#define BOSONOGA_HEADER_PATH "test/header.bosonoga"

#define BOSONOGA_RECORD typedef struct
#define BOSONOGA_SIZE size_t
#define BOSONOGA_COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

#define BOSONOGA_CALLOC(count, size) calloc(count, size)
#define BOSONOGA_FREE(ptr) free(ptr)
#define BOSONOGA_MEMCPY(dest, src, size) memcpy(dest, src, size)
#define BOSONOGA_MEMCHR(ptr, value, size) memchr(ptr, value, size)
#define BOSONOGA_STRLEN(str) strlen(str)
#define BOSONOGA_STRCHR(str, character) strchr(str, character)
#define BOSONOGA_STRCMP(a, b) strcmp(a, b)
#define BOSONOGA_STRNCMP(a, b, size) strncmp(a, b, size)

#define BOSONOGA_FILE FILE
#define BOSONOGA_STDERR stderr
#define BOSONOGA_EOF EOF
#define BOSONOGA_SEEK_SET SEEK_SET
#define BOSONOGA_SEEK_END SEEK_END
#if defined(_WIN32) && defined(_MSC_VER)
#define BOSONOGA_FOPEN(file, path, mode) fopen_s(file, path, mode)
#else
#define BOSONOGA_FOPEN(file, path, mode) ((*(file) = fopen(path, mode)) == NULL)
#endif
#define BOSONOGA_FSEEK(file, offset, origin) fseek(file, offset, origin)
#define BOSONOGA_FTELL(file) ftell(file)
#define BOSONOGA_FREAD(dest, size, count, file) fread(dest, size, count, file)
#define BOSONOGA_FGETC(file) fgetc(file)
#define BOSONOGA_FERROR(file) ferror(file)
#define BOSONOGA_FCLOSE(file) fclose(file)
#define BOSONOGA_PRINTF(...) printf(__VA_ARGS__)
#define BOSONOGA_FPRINTF(stream, ...) fprintf(stream, __VA_ARGS__)

BOSONOGA_RECORD { char data[BOSONOGA_LINE_LENGTH + 1]; }
BosonogaLine;

BOSONOGA_RECORD { char data[BOSONOGA_TOKEN_LENGTH + 1]; }
BosonogaToken;

BOSONOGA_RECORD {
  char string[BOSONOGA_TOKEN_LENGTH + 1];
  BOSONOGA_SIZE len;
}
BosonogaVersion;

BOSONOGA_RECORD {
  char legal[BOSONOGA_TOKEN_LENGTH + 1];
  BOSONOGA_SIZE len;
}
BosonogaLega;

BOSONOGA_RECORD {
  char original[BOSONOGA_TOKEN_LENGTH + 1];
  char replacement[BOSONOGA_TOKEN_LENGTH + 1];
  BOSONOGA_SIZE original_len;
  BOSONOGA_SIZE replacement_len;
}
BosonogaAka;

BOSONOGA_RECORD {
  char name[BOSONOGA_TOKEN_LENGTH + 1];
  int32_t value;
  uint32_t flags;
  bool is_flags;
}
BosonogaVariable;

BOSONOGA_RECORD {
  BosonogaVariable variables[BOSONOGA_VARIABLE_LIMIT];
  BOSONOGA_SIZE variable_count;
  BosonogaVersion version[BOSONOGA_VERSION_LIMIT];
  BosonogaLega lega[BOSONOGA_LEGA_LIMIT];
  BosonogaAka aka[BOSONOGA_AKA_LIMIT];
  BOSONOGA_SIZE version_count;
  BOSONOGA_SIZE lega_count;
  BOSONOGA_SIZE aka_count;
  BOSONOGA_SIZE main_count;
  BosonogaLine lines[BOSONOGA_LINE_LIMIT];
  BosonogaToken tokens[BOSONOGA_TOKEN_LIMIT];
  BOSONOGA_SIZE line_count;
  BOSONOGA_SIZE token_count;
}
Regina;

BOSONOGA_RECORD {
  const char* path;
  int expected;
}
BosonogaTest;

static const BosonogaTest tests[] = {
    {"test/gt_success.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/gt_failure.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/lt_success.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/lt_failure.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/and_success.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/and_failure.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/or_success.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/or_failure.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/eq_success.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/eq_failure.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/not_eq_success.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/not_eq_failure.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/eq_and_success.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/eq_and_failure.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/to_add.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/to_add_multiple.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/to_subtract.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/to_subtract_multiple.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/shift.bosonoga", BOSONOGA_EXIT_SUCCESS},
};

static bool strings_equal(const char* left, const char* right) {
  return BOSONOGA_STRCMP(left, right) == BOSONOGA_STRINGS_EQUAL;
}

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

static bool is_blank(char character) {
  return character == ' ' || character == '\t';
}

static bool is_digit(char character) {
  return character >= '0' && character <= '9';
}

static BOSONOGA_SIZE copy_text(char* destination, const char* source) {
  const BOSONOGA_SIZE size = BOSONOGA_STRLEN(source) + 1;
  BOSONOGA_MEMCPY(destination, source, size);
  return size;
}

static const char* token_at(const Regina* regina, BOSONOGA_SIZE position) {
  return regina->tokens[position].data;
}

static BOSONOGA_SIZE tokens_left(const Regina* regina, BOSONOGA_SIZE position) {
  return regina->token_count - position;
}

static int split_into_lines(const char* text, Regina* regina) {
  while (*text != '\0') {
    if (regina->line_count == BOSONOGA_LINE_LIMIT) {
      return BOSONOGA_EXIT_FAILURE;
    }

    const char* newline = BOSONOGA_STRCHR(text, '\n');
    const BOSONOGA_SIZE raw_length = newline == NULL
                                         ? BOSONOGA_STRLEN(text)
                                         : (BOSONOGA_SIZE)(newline - text);
    BOSONOGA_SIZE length = raw_length;
    if (length > 0 && text[length - 1] == '\r') {
      length--;
    }
    if (length > BOSONOGA_LINE_LENGTH) {
      return BOSONOGA_EXIT_FAILURE;
    }

    BosonogaLine* line = &regina->lines[regina->line_count++];
    BOSONOGA_MEMCPY(line->data, text, length);
    line->data[length] = '\0';

    text += raw_length;
    if (*text == '\n') {
      text++;
    }
  }
  return BOSONOGA_EXIT_SUCCESS;
}

static int split_line_into_tokens(const char* line, Regina* regina) {
  BOSONOGA_SIZE line_token_count = 0;
  while (true) {
    while (is_blank(*line)) {
      line++;
    }
    if (*line == '\0') {
      return BOSONOGA_EXIT_SUCCESS;
    }

    const char* start = line;
    while (*line != '\0' && !is_blank(*line)) {
      line++;
    }

    const BOSONOGA_SIZE length = (BOSONOGA_SIZE)(line - start);
    if (length > BOSONOGA_TOKEN_LENGTH ||
        line_token_count == BOSONOGA_TOKEN_PER_LINE_LIMIT) {
      return BOSONOGA_EXIT_FAILURE;
    }

    BosonogaToken* token = &regina->tokens[regina->token_count++];
    BOSONOGA_MEMCPY(token->data, start, length);
    token->data[length] = '\0';
    line_token_count++;
  }
}

static int split_lines_into_tokens(Regina* regina) {
  for (BOSONOGA_SIZE i = 0; i < regina->line_count; i++) {
    if (split_line_into_tokens(regina->lines[i].data, regina) !=
        BOSONOGA_EXIT_SUCCESS) {
      return BOSONOGA_EXIT_FAILURE;
    }
  }
  return BOSONOGA_EXIT_SUCCESS;
}

static int parse_header(Regina* regina) {
  for (BOSONOGA_SIZE position = 0; position < regina->token_count; position++) {
    const char* keyword = token_at(regina, position);

    if (strings_equal(keyword, "main")) {
      if (regina->main_count == BOSONOGA_MAIN_LIMIT) {
        return BOSONOGA_EXIT_FAILURE;
      }
      regina->main_count++;
    } else if (strings_equal(keyword, "version")) {
      if (regina->version_count == BOSONOGA_VERSION_LIMIT ||
          tokens_left(regina, position) < BOSONOGA_VERSION_TOKEN_COUNT) {
        return BOSONOGA_EXIT_FAILURE;
      }
      BosonogaVersion* version = &regina->version[regina->version_count++];
      version->len = copy_text(version->string, token_at(regina, position + 1));
      position += BOSONOGA_VERSION_TOKEN_COUNT - 1;
    } else if (strings_equal(keyword, "lega")) {
      if (regina->lega_count == BOSONOGA_LEGA_LIMIT ||
          tokens_left(regina, position) < BOSONOGA_LEGA_TOKEN_COUNT) {
        return BOSONOGA_EXIT_FAILURE;
      }
      BosonogaLega* lega = &regina->lega[regina->lega_count++];
      lega->len = copy_text(lega->legal, token_at(regina, position + 1));
      position += BOSONOGA_LEGA_TOKEN_COUNT - 1;
    } else if (strings_equal(keyword, "aka")) {
      if (regina->aka_count == BOSONOGA_AKA_LIMIT ||
          tokens_left(regina, position) < BOSONOGA_AKA_TOKEN_COUNT) {
        return BOSONOGA_EXIT_FAILURE;
      }
      BosonogaAka* aka = &regina->aka[regina->aka_count++];
      aka->original_len =
          copy_text(aka->original, token_at(regina, position + 1));
      aka->replacement_len =
          copy_text(aka->replacement, token_at(regina, position + 2));
      position += BOSONOGA_AKA_TOKEN_COUNT - 1;
    }
  }
  return BOSONOGA_EXIT_SUCCESS;
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
         strings_equal(token, "if");
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

static int run_main(Regina* regina) {
  BOSONOGA_SIZE position = position_after_main(regina);
  while (position < regina->token_count) {
    const char* keyword = token_at(regina, position);
    int status;
    if (strings_equal(keyword, "var")) {
      status = run_var(regina, &position);
    } else if (strings_equal(keyword, "set")) {
      status = run_set(regina, &position);
    } else if (strings_equal(keyword, "if")) {
      return run_if(regina, position);
    } else {
      return BOSONOGA_EXIT_FAILURE;
    }
    if (status != BOSONOGA_EXIT_SUCCESS) {
      return status;
    }
  }
  return BOSONOGA_EXIT_FAILURE;
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

static int load_program(Regina* regina, const char* header, const char* input) {
  if (split_into_lines(header, regina) != BOSONOGA_EXIT_SUCCESS ||
      split_into_lines(input, regina) != BOSONOGA_EXIT_SUCCESS ||
      split_lines_into_tokens(regina) != BOSONOGA_EXIT_SUCCESS ||
      parse_header(regina) != BOSONOGA_EXIT_SUCCESS) {
    return BOSONOGA_EXIT_FAILURE;
  }
  return BOSONOGA_EXIT_SUCCESS;
}

static int core(const char* header, const char* input) {
  Regina* regina = BOSONOGA_CALLOC(1, sizeof(Regina));
  if (regina == NULL) {
    return BOSONOGA_EXIT_FAILURE;
  }

  int status = load_program(regina, header, input);
  if (status == BOSONOGA_EXIT_SUCCESS) {
    status = run_main(regina);
  }
  if (BOSONOGA_LOG_PARSETOK) {
    log_regina(regina);
  }

  BOSONOGA_FREE(regina);
  return status;
}

static int measure_source(BOSONOGA_FILE* file, BOSONOGA_SIZE* length) {
  if (BOSONOGA_FSEEK(file, 0, BOSONOGA_SEEK_END) != BOSONOGA_IO_SUCCESS) {
    return BOSONOGA_EXIT_FAILURE;
  }
  const long end = BOSONOGA_FTELL(file);
  if (end < 0 || end > BOSONOGA_SOURCE_LENGTH ||
      BOSONOGA_FSEEK(file, 0, BOSONOGA_SEEK_SET) != BOSONOGA_IO_SUCCESS) {
    return BOSONOGA_EXIT_FAILURE;
  }
  *length = (BOSONOGA_SIZE)end;
  return BOSONOGA_EXIT_SUCCESS;
}

static int read_exactly(BOSONOGA_FILE* file, char* source,
                        BOSONOGA_SIZE length) {
  const BOSONOGA_SIZE read_count = BOSONOGA_FREAD(source, 1, length, file);
  if (read_count != length || BOSONOGA_FGETC(file) != BOSONOGA_EOF ||
      BOSONOGA_FERROR(file) != BOSONOGA_IO_SUCCESS ||
      BOSONOGA_MEMCHR(source, '\0', length) != NULL) {
    return BOSONOGA_EXIT_FAILURE;
  }
  source[length] = '\0';
  return BOSONOGA_EXIT_SUCCESS;
}

static int read_source(const char* path,
                       char source[BOSONOGA_SOURCE_LENGTH + 1]) {
  source[0] = '\0';
  BOSONOGA_FILE* file = NULL;
  if (BOSONOGA_FOPEN(&file, path, "rb") != BOSONOGA_IO_SUCCESS) {
    return BOSONOGA_EXIT_FAILURE;
  }

  BOSONOGA_SIZE length = 0;
  int status = measure_source(file, &length);
  if (status == BOSONOGA_EXIT_SUCCESS) {
    status = read_exactly(file, source, length);
  }
  if (BOSONOGA_FCLOSE(file) != BOSONOGA_IO_SUCCESS) {
    status = BOSONOGA_EXIT_FAILURE;
  }
  if (status != BOSONOGA_EXIT_SUCCESS) {
    source[0] = '\0';
  }
  return status;
}

static bool run_test(const BosonogaTest* test, const char* header,
                     char input[BOSONOGA_SOURCE_LENGTH + 1]) {
  if (read_source(test->path, input) != BOSONOGA_EXIT_SUCCESS) {
    BOSONOGA_FPRINTF(BOSONOGA_STDERR, "FAIL %s: unable to load source\n",
                     test->path);
    return false;
  }

  const int status = core(header, input);
  if (status != test->expected) {
    BOSONOGA_FPRINTF(BOSONOGA_STDERR, "FAIL %s: expected %d, got %d\n",
                     test->path, test->expected, status);
    return false;
  }
  return true;
}

int main(void) {
  char header[BOSONOGA_SOURCE_LENGTH + 1];
  char input[BOSONOGA_SOURCE_LENGTH + 1];
  if (read_source(BOSONOGA_HEADER_PATH, header) != BOSONOGA_EXIT_SUCCESS) {
    BOSONOGA_FPRINTF(BOSONOGA_STDERR, "Unable to load %s\n",
                     BOSONOGA_HEADER_PATH);
    return BOSONOGA_EXIT_FAILURE;
  }

  const BOSONOGA_SIZE test_count = BOSONOGA_COUNT_OF(tests);
  BOSONOGA_SIZE passed = 0;
  for (BOSONOGA_SIZE i = 0; i < test_count; i++) {
    if (run_test(&tests[i], header, input)) {
      passed++;
    }
  }

  BOSONOGA_PRINTF("Tests passed: %zu/%zu\n", passed, test_count);
  return passed == test_count ? BOSONOGA_EXIT_SUCCESS : BOSONOGA_EXIT_FAILURE;
}
