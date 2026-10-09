#include <stdio.h>

#include "bosonoga/bosonoga.h"

#define BOSONOGA_HEADER_PATH "test/header.bosonoga"
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

  const int status = bosonoga_core(header, input);
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
