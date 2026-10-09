#ifndef BOSONOGA_H
#define BOSONOGA_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOSONOGA_VERSION_LIMIT 1
#define BOSONOGA_LEGA_LIMIT 16
#define BOSONOGA_AKA_LIMIT 16
#define BOSONOGA_VARIABLE_LIMIT 64
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

#define BOSONOGA_EXIT_SUCCESS 0
#define BOSONOGA_EXIT_FAILURE 1
#define BOSONOGA_STRINGS_EQUAL 0

#define BOSONOGA_RECORD typedef struct
#define BOSONOGA_SIZE size_t

#define BOSONOGA_CALLOC(count, size) calloc(count, size)
#define BOSONOGA_FREE(ptr) free(ptr)
#define BOSONOGA_MEMCPY(dest, src, size) memcpy(dest, src, size)
#define BOSONOGA_MEMCHR(ptr, value, size) memchr(ptr, value, size)
#define BOSONOGA_STRLEN(str) strlen(str)
#define BOSONOGA_STRCHR(str, character) strchr(str, character)
#define BOSONOGA_STRCMP(a, b) strcmp(a, b)
#define BOSONOGA_STRNCMP(a, b, size) strncmp(a, b, size)
#define BOSONOGA_PRINTF(...) printf(__VA_ARGS__)

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

static inline bool strings_equal(const char* left, const char* right) {
  return BOSONOGA_STRCMP(left, right) == BOSONOGA_STRINGS_EQUAL;
}

static inline BOSONOGA_SIZE copy_text(char* destination, const char* source) {
  const BOSONOGA_SIZE size = BOSONOGA_STRLEN(source) + 1;
  BOSONOGA_MEMCPY(destination, source, size);
  return size;
}

static inline const char* token_at(const Regina* regina,
                                   BOSONOGA_SIZE position) {
  return regina->tokens[position].data;
}

static inline BOSONOGA_SIZE tokens_left(const Regina* regina,
                                        BOSONOGA_SIZE position) {
  return regina->token_count - position;
}

int bosonoga_load_program(Regina* regina, const char* header,
                          const char* input);
int bosonoga_core(const char* header, const char* input);

#endif
