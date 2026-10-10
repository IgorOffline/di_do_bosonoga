#include <stdio.h>

#include "bosonoga/bosonoga.h"

#define BOSONOGA_HEADER_PATH "test/header.bosonoga"
#define BOSONOGA_REJECT_PATH "test/reject.bosonoga"
#define BOSONOGA_CASE_SEPARATOR "-----"
#define BOSONOGA_CASE_LIMIT 64
#define BOSONOGA_IO_SUCCESS 0
#define BOSONOGA_COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

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
#define BOSONOGA_FPRINTF(stream, ...) fprintf(stream, __VA_ARGS__)

BOSONOGA_RECORD {
  const char* path;
  int expected;
}
BosonogaTest;

static const BosonogaTest tests[] = {
    {"test/compare.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/branch.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/arithmetic.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/f32.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/loop.bosonoga", BOSONOGA_EXIT_SUCCESS},
    {"test/shift.bosonoga", BOSONOGA_EXIT_SUCCESS},
};

static int measure_source(BOSONOGA_FILE* file, BOSONOGA_SIZE* length) {
  if (BOSONOGA_FSEEK(file, 0, BOSONOGA_SEEK_END) != BOSONOGA_IO_SUCCESS) {
    return BOSONOGA_EXIT_FAILURE;
  }
  const long end = BOSONOGA_FTELL(file);
  if (end < 0 || (BOSONOGA_SIZE)end > BOSONOGA_SOURCE_LENGTH ||
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

static bool run_test(Regina* regina, const BosonogaTest* test) {
  if (read_source(test->path, regina->input_source) != BOSONOGA_EXIT_SUCCESS) {
    BOSONOGA_FPRINTF(BOSONOGA_STDERR, "FAIL %s: unable to load source\n",
                     test->path);
    return false;
  }

  const int status =
      bosonoga_core(regina, regina->header_source, regina->input_source);
  if (status != test->expected) {
    BOSONOGA_FPRINTF(BOSONOGA_STDERR, "FAIL %s: expected %d, got %d\n",
                     test->path, test->expected, status);
    return false;
  }
  return true;
}

static bool is_blank_text(const char* text) {
  for (; *text != '\0'; text++) {
    if (*text != ' ' && *text != '\t' && *text != '\r' && *text != '\n') {
      return false;
    }
  }
  return true;
}

static BOSONOGA_SIZE split_cases(char* source, char** starts) {
  const BOSONOGA_SIZE separator_length =
      BOSONOGA_STRLEN(BOSONOGA_CASE_SEPARATOR);
  BOSONOGA_SIZE count = 0;
  starts[count++] = source;
  char* line = source;
  while (*line != '\0') {
    char* newline = BOSONOGA_STRCHR(line, '\n');
    char* next = newline == NULL ? line + BOSONOGA_STRLEN(line) : newline + 1;
    BOSONOGA_SIZE length = (BOSONOGA_SIZE)(next - line);
    while (length > 0 &&
           (line[length - 1] == '\n' || line[length - 1] == '\r')) {
      length--;
    }
    if (length == separator_length &&
        BOSONOGA_STRNCMP(line, BOSONOGA_CASE_SEPARATOR, separator_length) ==
            BOSONOGA_STRINGS_EQUAL) {
      if (count == BOSONOGA_CASE_LIMIT) {
        return 0;
      }
      *line = '\0';
      starts[count++] = next;
    }
    line = next;
  }
  return count;
}

static BOSONOGA_SIZE run_reject_cases(Regina* regina,
                                      BOSONOGA_SIZE* case_count) {
  *case_count = 1;
  if (read_source(BOSONOGA_REJECT_PATH, regina->input_source) !=
      BOSONOGA_EXIT_SUCCESS) {
    BOSONOGA_FPRINTF(BOSONOGA_STDERR, "FAIL %s: unable to load source\n",
                     BOSONOGA_REJECT_PATH);
    return 0;
  }
  char* starts[BOSONOGA_CASE_LIMIT];
  const BOSONOGA_SIZE count = split_cases(regina->input_source, starts);
  if (count == 0) {
    BOSONOGA_FPRINTF(BOSONOGA_STDERR, "FAIL %s: more than %d cases\n",
                     BOSONOGA_REJECT_PATH, BOSONOGA_CASE_LIMIT);
    return 0;
  }

  *case_count = count;
  BOSONOGA_SIZE passed = 0;
  for (BOSONOGA_SIZE i = 0; i < count; i++) {
    if (is_blank_text(starts[i])) {
      BOSONOGA_FPRINTF(BOSONOGA_STDERR, "FAIL %s case %zu: empty case\n",
                       BOSONOGA_REJECT_PATH, i + 1);
    } else if (bosonoga_core(regina, regina->header_source, starts[i]) ==
               BOSONOGA_EXIT_SUCCESS) {
      BOSONOGA_FPRINTF(BOSONOGA_STDERR, "FAIL %s case %zu: was accepted\n",
                       BOSONOGA_REJECT_PATH, i + 1);
    } else {
      passed++;
    }
  }
  return passed;
}

int main(void) {
  Regina* regina = BOSONOGA_CALLOC(1, sizeof(Regina));
  if (regina == NULL) {
    BOSONOGA_FPRINTF(BOSONOGA_STDERR, "Unable to allocate Regina (%zu MiB)\n",
                     sizeof(Regina) / BOSONOGA_MIB);
    return BOSONOGA_EXIT_FAILURE;
  }
  if (read_source(BOSONOGA_HEADER_PATH, regina->header_source) !=
      BOSONOGA_EXIT_SUCCESS) {
    BOSONOGA_FPRINTF(BOSONOGA_STDERR, "Unable to load %s\n",
                     BOSONOGA_HEADER_PATH);
    BOSONOGA_FREE(regina);
    return BOSONOGA_EXIT_FAILURE;
  }

  const BOSONOGA_SIZE test_count = BOSONOGA_COUNT_OF(tests);
  BOSONOGA_SIZE passed = 0;
  for (BOSONOGA_SIZE i = 0; i < test_count; i++) {
    if (run_test(regina, &tests[i])) {
      passed++;
    }
  }

  BOSONOGA_SIZE reject_count = 0;
  passed += run_reject_cases(regina, &reject_count);
  const BOSONOGA_SIZE total = test_count + reject_count;

  BOSONOGA_PRINTF("Tests passed: %zu/%zu (%zu, %zu)\n", passed, total,
                  test_count, reject_count);
  BOSONOGA_FREE(regina);
  return passed == total ? BOSONOGA_EXIT_SUCCESS : BOSONOGA_EXIT_FAILURE;
}
