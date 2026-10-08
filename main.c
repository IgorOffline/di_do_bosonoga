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
#define BOSONOGA_LINE_LIMIT 256
#define BOSONOGA_LINE_LENGTH 255
#define BOSONOGA_TOKEN_PER_LINE_LIMIT 16
#define BOSONOGA_TOKEN_LENGTH 63
#define BOSONOGA_TOKEN_LIMIT \
  (BOSONOGA_LINE_LIMIT * BOSONOGA_TOKEN_PER_LINE_LIMIT)

#define BOSONOGA_EXIT_SUCCESS 0
#define BOSONOGA_EXIT_FAILURE 1
#define BOSONOGA_STRINGS_EQUAL 0

#define BOSONOGA_LOG_PARSETOK true
#define BOSONOGA_RECORD typedef struct
#define BOSONOGA_SIZE size_t
#define BOSONOGA_MALLOC(size) malloc(size)
#define BOSONOGA_FREE(ptr) free(ptr)
#define BOSONOGA_MEMCPY(dest, src, size) memcpy(dest, src, size)
#define BOSONOGA_MEMSET(dest, value, size) memset(dest, value, size)
#define BOSONOGA_STRLEN(str) strlen(str)
#define BOSONOGA_STRCMP(a, b) strcmp(a, b)

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

static int parse_input(const char* input, Regina* regina) {
  while (*input != '\0') {
    if (regina->line_count == BOSONOGA_LINE_LIMIT) {
      return BOSONOGA_EXIT_FAILURE;
    }

    const char* end = strchr(input, '\n');
    BOSONOGA_SIZE length =
        end == NULL ? BOSONOGA_STRLEN(input) : (BOSONOGA_SIZE)(end - input);

    if (length > BOSONOGA_LINE_LENGTH) {
      return BOSONOGA_EXIT_FAILURE;
    }

    BosonogaLine* line = &regina->lines[regina->line_count++];

    BOSONOGA_MEMCPY(line->data, input, length);
    line->data[length] = '\0';

    input += length;

    if (*input == '\n') {
      input++;
    }
  }

  return BOSONOGA_EXIT_SUCCESS;
}

static int parse_lines(Regina* regina) {
  for (BOSONOGA_SIZE i = 0; i < regina->line_count; i++) {
    const char* current = regina->lines[i].data;
    BOSONOGA_SIZE line_tokens = 0;

    while (*current != '\0') {
      while (*current == ' ') {
        current++;
      }

      if (*current == '\0') {
        break;
      }

      const char* start = current;

      while (*current != '\0' && *current != ' ') {
        current++;
      }

      BOSONOGA_SIZE length = (BOSONOGA_SIZE)(current - start);

      if (length > BOSONOGA_TOKEN_LENGTH ||
          line_tokens == BOSONOGA_TOKEN_PER_LINE_LIMIT) {
        return BOSONOGA_EXIT_FAILURE;
      }

      BosonogaToken* token = &regina->tokens[regina->token_count++];

      BOSONOGA_MEMCPY(token->data, start, length);
      token->data[length] = '\0';

      line_tokens++;
    }
  }

  return BOSONOGA_EXIT_SUCCESS;
}

static int parse_records(Regina* regina) {
  for (BOSONOGA_SIZE i = 0; i < regina->token_count; i++) {
    const char* keyword = regina->tokens[i].data;

    if (BOSONOGA_STRCMP(keyword, "main") == BOSONOGA_STRINGS_EQUAL) {
      if (regina->main_count == BOSONOGA_MAIN_LIMIT) {
        return BOSONOGA_EXIT_FAILURE;
      }
      regina->main_count++;
    } else if (BOSONOGA_STRCMP(keyword, "version") == BOSONOGA_STRINGS_EQUAL) {
      if (regina->version_count == BOSONOGA_VERSION_LIMIT ||
          regina->token_count - i < 2) {
        return BOSONOGA_EXIT_FAILURE;
      }
      BosonogaVersion* version = &regina->version[regina->version_count++];
      const char* string = regina->tokens[++i].data;
      version->len = BOSONOGA_STRLEN(string) + 1;
      BOSONOGA_MEMCPY(version->string, string, version->len);
    } else if (BOSONOGA_STRCMP(keyword, "lega") == BOSONOGA_STRINGS_EQUAL) {
      if (regina->lega_count == BOSONOGA_LEGA_LIMIT ||
          regina->token_count - i < 2) {
        return BOSONOGA_EXIT_FAILURE;
      }
      BosonogaLega* lega = &regina->lega[regina->lega_count++];
      const char* legal = regina->tokens[++i].data;
      lega->len = BOSONOGA_STRLEN(legal) + 1;
      BOSONOGA_MEMCPY(lega->legal, legal, lega->len);
    } else if (BOSONOGA_STRCMP(keyword, "aka") == BOSONOGA_STRINGS_EQUAL) {
      if (regina->aka_count == BOSONOGA_AKA_LIMIT ||
          regina->token_count - i < 3) {
        return BOSONOGA_EXIT_FAILURE;
      }
      BosonogaAka* aka = &regina->aka[regina->aka_count++];
      const char* original = regina->tokens[++i].data;
      const char* replacement = regina->tokens[++i].data;
      aka->original_len = BOSONOGA_STRLEN(original) + 1;
      aka->replacement_len = BOSONOGA_STRLEN(replacement) + 1;
      BOSONOGA_MEMCPY(aka->original, original, aka->original_len);
      BOSONOGA_MEMCPY(aka->replacement, replacement, aka->replacement_len);
    }
  }

  return BOSONOGA_EXIT_SUCCESS;
}

static void print_tokens(const Regina* regina) {
  for (BOSONOGA_SIZE i = 0; i < regina->token_count; i++) {
    if (BOSONOGA_STRCMP(regina->tokens[i].data, "version") ==
        BOSONOGA_STRINGS_EQUAL) {
      if (BOSONOGA_LOG_PARSETOK) {
        printf("[V]\n");
      }
      i++;
    } else if (BOSONOGA_STRCMP(regina->tokens[i].data, "lega") ==
               BOSONOGA_STRINGS_EQUAL) {
      if (BOSONOGA_LOG_PARSETOK) {
        printf("[L]\n");
      }
      i++;
    } else if (BOSONOGA_STRCMP(regina->tokens[i].data, "aka") ==
               BOSONOGA_STRINGS_EQUAL) {
      if (BOSONOGA_LOG_PARSETOK) {
        printf("[A]\n");
      }
      i += 2;
    } else if (BOSONOGA_STRCMP(regina->tokens[i].data, "main") ==
               BOSONOGA_STRINGS_EQUAL) {
      if (BOSONOGA_LOG_PARSETOK) {
        printf("[M]\n");
      }
    } else {
      if (BOSONOGA_LOG_PARSETOK) {
        printf("[%s]\n", regina->tokens[i].data);
      }
    }
  }
}

static const BosonogaVariable* find_variable(const Regina* regina,
                                             const char* name) {
  for (BOSONOGA_SIZE i = 0; i < regina->variable_count; i++) {
    if (BOSONOGA_STRCMP(regina->variables[i].name, name) ==
        BOSONOGA_STRINGS_EQUAL) {
      return &regina->variables[i];
    }
  }
  return NULL;
}

static int parse_return_status(const char* keyword, int* status) {
  if (BOSONOGA_STRCMP(keyword, "exit_success") == BOSONOGA_STRINGS_EQUAL) {
    *status = BOSONOGA_EXIT_SUCCESS;
  } else if (BOSONOGA_STRCMP(keyword, "exit_failure") ==
             BOSONOGA_STRINGS_EQUAL) {
    *status = BOSONOGA_EXIT_FAILURE;
  } else {
    return BOSONOGA_EXIT_FAILURE;
  }
  return BOSONOGA_EXIT_SUCCESS;
}

static int parse_tokens(Regina* regina) {
  bool inside_main = false;
  for (BOSONOGA_SIZE i = 0; i < regina->token_count; i++) {
    const char* keyword = regina->tokens[i].data;
    if (BOSONOGA_STRCMP(keyword, "main") == BOSONOGA_STRINGS_EQUAL) {
      inside_main = true;
      continue;
    }

    if (inside_main &&
        BOSONOGA_STRCMP(keyword, "var") == BOSONOGA_STRINGS_EQUAL) {
      if (regina->token_count - i < 4) {
        return BOSONOGA_EXIT_FAILURE;
      }
      if (BOSONOGA_STRCMP(regina->tokens[i + 2].data, "is") !=
          BOSONOGA_STRINGS_EQUAL) {
        return BOSONOGA_EXIT_FAILURE;
      }

      const char* name = regina->tokens[i + 1].data;
      if (find_variable(regina, name) != NULL) {
        return BOSONOGA_EXIT_FAILURE;
      }
      const char* i32_value_raw = regina->tokens[i + 3].data;
      const char* suffix = "_i32";
      const BOSONOGA_SIZE raw_len = BOSONOGA_STRLEN(i32_value_raw);
      const BOSONOGA_SIZE suffix_len = BOSONOGA_STRLEN(suffix);

      if (raw_len <= suffix_len ||
          BOSONOGA_STRCMP(i32_value_raw + raw_len - suffix_len, suffix) !=
              BOSONOGA_STRINGS_EQUAL) {
        return BOSONOGA_EXIT_FAILURE;
      }

      if (regina->variable_count == BOSONOGA_VARIABLE_LIMIT) {
        return BOSONOGA_EXIT_FAILURE;
      }
      const BOSONOGA_SIZE value_len = raw_len - suffix_len;
      BOSONOGA_SIZE digit = 0;
      const bool negative = i32_value_raw[0] == '-';
      if (negative) digit++;
      if (digit == value_len) return BOSONOGA_EXIT_FAILURE;
      int64_t value = 0;
      const int64_t limit = negative ? -(int64_t)INT32_MIN : INT32_MAX;
      for (; digit < value_len; digit++) {
        if (i32_value_raw[digit] < '0' || i32_value_raw[digit] > '9') {
          return BOSONOGA_EXIT_FAILURE;
        }
        value = value * 10 + (i32_value_raw[digit] - '0');
        if (value > limit) return BOSONOGA_EXIT_FAILURE;
      }
      BosonogaVariable* variable = &regina->variables[regina->variable_count++];
      BOSONOGA_MEMCPY(variable->name, name, BOSONOGA_STRLEN(name) + 1);
      variable->value = (int32_t)(negative ? -value : value);
      i += 3;
    } else if (inside_main &&
               BOSONOGA_STRCMP(keyword, "if") == BOSONOGA_STRINGS_EQUAL) {
      BOSONOGA_SIZE position = i + 1;
      bool condition = false;
      bool and_group = true;
      for (;;) {
        if (regina->token_count - position < 3) return BOSONOGA_EXIT_FAILURE;
        const BosonogaVariable* left =
            find_variable(regina, regina->tokens[position].data);
        const char* operation = regina->tokens[position + 1].data;
        const BosonogaVariable* right =
            find_variable(regina, regina->tokens[position + 2].data);
        if (left == NULL || right == NULL) return BOSONOGA_EXIT_FAILURE;
        bool comparison;
        if (BOSONOGA_STRCMP(operation, "gt") == BOSONOGA_STRINGS_EQUAL) {
          comparison = left->value > right->value;
        } else if (BOSONOGA_STRCMP(operation, "lt") == BOSONOGA_STRINGS_EQUAL) {
          comparison = left->value < right->value;
        } else {
          return BOSONOGA_EXIT_FAILURE;
        }
        and_group = and_group && comparison;
        position += 3;
        if (position == regina->token_count) return BOSONOGA_EXIT_FAILURE;
        if (BOSONOGA_STRCMP(regina->tokens[position].data, "or") ==
            BOSONOGA_STRINGS_EQUAL) {
          condition = condition || and_group;
          and_group = true;
        } else if (BOSONOGA_STRCMP(regina->tokens[position].data, "and") !=
                   BOSONOGA_STRINGS_EQUAL) {
          break;
        }
        position++;
      }
      condition = condition || and_group;
      if (regina->token_count - position != 6) return BOSONOGA_EXIT_FAILURE;
      const BosonogaToken* branches = &regina->tokens[position];
      if (BOSONOGA_STRCMP(branches[0].data, "di") != BOSONOGA_STRINGS_EQUAL ||
          BOSONOGA_STRCMP(branches[1].data, "return") !=
              BOSONOGA_STRINGS_EQUAL ||
          BOSONOGA_STRCMP(branches[3].data, "do") != BOSONOGA_STRINGS_EQUAL ||
          BOSONOGA_STRCMP(branches[4].data, "return") !=
              BOSONOGA_STRINGS_EQUAL) {
        return BOSONOGA_EXIT_FAILURE;
      }
      int di_status;
      int do_status;
      if (parse_return_status(branches[2].data, &di_status) !=
              BOSONOGA_EXIT_SUCCESS ||
          parse_return_status(branches[5].data, &do_status) !=
              BOSONOGA_EXIT_SUCCESS) {
        return BOSONOGA_EXIT_FAILURE;
      }
      return condition ? di_status : do_status;
    } else if (inside_main) {
      return BOSONOGA_EXIT_FAILURE;
    }
  }

  return BOSONOGA_EXIT_FAILURE;
}

static void print_non_empty_regina(const Regina* regina) {
  for (BOSONOGA_SIZE i = 0; i < regina->version_count; i++) {
    const BosonogaVersion* version = &regina->version[i];
    if (version->string[0] != '\0') {
      if (BOSONOGA_LOG_PARSETOK) {
        printf("version[%zu]: %s (len=%zu)\n", i, version->string,
               version->len);
      }
    }
  }

  for (BOSONOGA_SIZE i = 0; i < regina->lega_count; i++) {
    const BosonogaLega* lega = &regina->lega[i];
    if (lega->legal[0] != '\0') {
      if (BOSONOGA_LOG_PARSETOK) {
        printf("lega[%zu]: %s (len=%zu)\n", i, lega->legal, lega->len);
      }
    }
  }

  for (BOSONOGA_SIZE i = 0; i < regina->aka_count; i++) {
    const BosonogaAka* aka = &regina->aka[i];
    if (aka->original[0] != '\0') {
      if (BOSONOGA_LOG_PARSETOK) {
        printf("aka[%zu].original: %s (len=%zu)\n", i, aka->original,
               aka->original_len);
      }
    }
    if (aka->replacement[0] != '\0') {
      if (BOSONOGA_LOG_PARSETOK) {
        printf("aka[%zu].replacement: %s (len=%zu)\n", i, aka->replacement,
               aka->replacement_len);
      }
    }
  }

  if (regina->main_count != 0) {
    if (BOSONOGA_LOG_PARSETOK) {
      printf("main_count: %zu\n", regina->main_count);
    }
  }

  for (BOSONOGA_SIZE i = 0; i < regina->line_count; i++) {
    if (regina->lines[i].data[0] != '\0') {
      if (BOSONOGA_LOG_PARSETOK) {
        printf("lines[%zu]: %s\n", i, regina->lines[i].data);
      }
    }
  }

  for (BOSONOGA_SIZE i = 0; i < regina->token_count; i++) {
    if (regina->tokens[i].data[0] != '\0') {
      if (BOSONOGA_LOG_PARSETOK) {
        printf("tokens[%zu]: %s\n", i, regina->tokens[i].data);
      }
    }
  }

  for (BOSONOGA_SIZE i = 0; i < regina->variable_count; i++) {
    if (BOSONOGA_LOG_PARSETOK) {
      printf("variables[%zu]: %s = %" PRId32 "\n", i, regina->variables[i].name,
             regina->variables[i].value);
    }
  }
}

static int core(const char* header, const char* input) {
  Regina* regina = BOSONOGA_MALLOC(sizeof(Regina));

  if (regina == NULL) {
    return BOSONOGA_EXIT_FAILURE;
  }

  BOSONOGA_MEMSET(regina, 0, sizeof(*regina));

  int status = parse_input(header, regina);
  if (status == BOSONOGA_EXIT_SUCCESS) {
    status = parse_input(input, regina);
  }

  if (BOSONOGA_LOG_PARSETOK) {
    printf("=== === === === ===\n");
    printf("Regina allocated: %zu bytes\n", sizeof(Regina));
    printf("=== === === === ===\n");
  }

  if (status == BOSONOGA_EXIT_SUCCESS) {
    status = parse_lines(regina);
  }

  if (status == BOSONOGA_EXIT_SUCCESS) {
    status = parse_records(regina);
  }

  if (status == BOSONOGA_EXIT_SUCCESS) {
    print_tokens(regina);
  }
  if (BOSONOGA_LOG_PARSETOK) {
    printf("=== === === === ===\n");
  }

  if (status == BOSONOGA_EXIT_SUCCESS) {
    status = parse_tokens(regina);
  }
  if (BOSONOGA_LOG_PARSETOK) {
    printf("=== === === === ===\n");
  }

  print_non_empty_regina(regina);
  if (BOSONOGA_LOG_PARSETOK) {
    printf("=== === === === ===\n");
  }

  BOSONOGA_FREE(regina);
  return status;
}

int main(void) {
  const char* const header =
      "version 0.2.0\n"
      "lega _bosonoga_if\n"
      "lega _bosonoga_gt\n"
      "lega _bosonoga_and\n"
      "lega _bosonoga_or\n"
      "lega _bosonoga_lt\n"
      "lega _bosonoga_di\n"
      "lega _bosonoga_return\n"
      "lega _bosonoga_exit_success\n"
      "lega _bosonoga_do\n"
      "lega _bosonoga_exit_failure\n"
      "aka _bosonoga_if if\n"
      "aka _bosonoga_gt gt\n"
      "aka _bosonoga_and and\n"
      "aka _bosonoga_or or\n"
      "aka _bosonoga_lt lt\n"
      "aka _bosonoga_di di\n"
      "aka _bosonoga_return return\n"
      "aka _bosonoga_exit_success exit_success\n"
      "aka _bosonoga_do do\n"
      "aka _bosonoga_exit_failure exit_failure\n"
      "main\n";

  const char* const input_gt_success =
      "var test_gt_alpha is 20_i32\n"
      "var test_gt_bravo is 10_i32\n"
      "if test_gt_alpha gt test_gt_bravo di return exit_success do return "
      "exit_failure\n";
  const char* const input_gt_failure =
      "var test_gt_charlie is 10_i32\n"
      "var test_gt_delta is 20_i32\n"
      "if test_gt_charlie gt test_gt_delta di return exit_failure do return "
      "exit_success\n";
  const char* const input_lt_success =
      "var test_lt_alpha is 10_i32\n"
      "var test_lt_bravo is 20_i32\n"
      "if test_lt_alpha lt test_lt_bravo di return exit_success do return "
      "exit_failure\n";
  const char* const input_lt_failure =
      "var test_lt_charlie is 20_i32\n"
      "var test_lt_delta is 10_i32\n"
      "if test_lt_charlie lt test_lt_delta di return exit_failure do return "
      "exit_success\n";
  const char* const input_and_success =
      "var test_and_echo is 30_i32\n"
      "var test_and_foxtrot is 20_i32\n"
      "var test_and_golf is 10_i32\n"
      "if test_and_echo gt test_and_foxtrot and test_and_foxtrot gt "
      "test_and_golf di return exit_success do return "
      "exit_failure\n";
  const char* const input_and_failure =
      "var test_and_hotel is 30_i32\n"
      "var test_and_india is 20_i32\n"
      "var test_and_julia is 25_i32\n"
      "if test_and_hotel gt test_and_india and test_and_india gt "
      "test_and_julia di return exit_failure do return "
      "exit_success\n";
  const char* const input_or_success =
      "var test_or_echo is 30_i32\n"
      "var test_or_foxtrot is 20_i32\n"
      "var test_or_golf is 25_i32\n"
      "if test_or_echo gt test_or_foxtrot or test_or_foxtrot gt "
      "test_or_golf di return exit_success do return "
      "exit_failure\n";
  const char* const input_or_failure =
      "var test_or_hotel is 20_i32\n"
      "var test_or_india is 25_i32\n"
      "var test_or_julia is 24_i32\n"
      "if test_or_hotel gt test_or_india or test_or_india gt test_or_julia di "
      "return exit_success do return "
      "exit_failure\n";

  const int status_input_gt_success = core(header, input_gt_success);
  const int status_input_gt_failure = core(header, input_gt_failure);
  const int status_input_lt_success = core(header, input_lt_success);
  const int status_input_lt_failure = core(header, input_lt_failure);
  const int status_input_and_success = core(header, input_and_success);
  const int status_input_and_failure = core(header, input_and_failure);
  const int status_input_or_success = core(header, input_or_success);
  const int status_input_or_failure = core(header, input_or_failure);
  printf("=== === === === ===\n");
  printf("[%d %d %d %d %d %d %d %d]\n", status_input_gt_success,
         status_input_gt_failure, status_input_lt_success,
         status_input_lt_failure, status_input_and_success,
         status_input_and_failure, status_input_or_success,
         status_input_or_failure);
  printf("=== === === === ===\n");

  return BOSONOGA_EXIT_SUCCESS;
}
