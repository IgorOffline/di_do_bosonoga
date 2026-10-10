#ifndef BOSONOGA_H
#define BOSONOGA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOSONOGA_MIB (1024U * 1024U)
#define BOSONOGA_REGINA_BYTES (240U * BOSONOGA_MIB)
#define BOSONOGA_SOURCE_ASPECT_BYTES (16U * BOSONOGA_MIB)
#define BOSONOGA_LINE_ASPECT_BYTES (64U * BOSONOGA_MIB)
#define BOSONOGA_TOKEN_ASPECT_BYTES (112U * BOSONOGA_MIB)
#define BOSONOGA_STATE_ASPECT_BYTES (48U * BOSONOGA_MIB)

#define BOSONOGA_SOURCE_LENGTH (BOSONOGA_SOURCE_ASPECT_BYTES / 2U - 1U)

#define BOSONOGA_LINE_LENGTH 255
#define BOSONOGA_LINE_LIMIT \
  (BOSONOGA_LINE_ASPECT_BYTES / (BOSONOGA_LINE_LENGTH + 1U) - 1U)

#define BOSONOGA_TOKEN_LENGTH 63
#define BOSONOGA_TOKEN_PER_LINE_LIMIT 128
#define BOSONOGA_TOKEN_LIMIT \
  (BOSONOGA_TOKEN_ASPECT_BYTES / (BOSONOGA_TOKEN_LENGTH + 1U) - 1U)

#define BOSONOGA_VERSION_LIMIT 1
#define BOSONOGA_LEGA_LIMIT 1024
#define BOSONOGA_AKA_LIMIT 1024
#define BOSONOGA_STATE_RESERVE_BYTES BOSONOGA_MIB
#define BOSONOGA_VARIABLE_LIMIT                                   \
  ((BOSONOGA_STATE_ASPECT_BYTES - BOSONOGA_STATE_RESERVE_BYTES) / \
   sizeof(BosonogaVariable))

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
#define BOSONOGA_STRTOD(text, end) strtod(text, end)
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

typedef enum {
  BOSONOGA_KIND_I32,
  BOSONOGA_KIND_F32,
  BOSONOGA_KIND_FLAGS
} BosonogaKind;

BOSONOGA_RECORD {
  BosonogaKind kind;
  int32_t i32;
  float f32;
  uint32_t flags;
}
BosonogaValue;

BOSONOGA_RECORD {
  char name[BOSONOGA_TOKEN_LENGTH + 1];
  BosonogaValue value;
}
BosonogaVariable;

BOSONOGA_RECORD {
  union {
    struct {
      char header_source[BOSONOGA_SOURCE_LENGTH + 1];
      char input_source[BOSONOGA_SOURCE_LENGTH + 1];
    };
    unsigned char source_aspect[BOSONOGA_SOURCE_ASPECT_BYTES];
  };
  union {
    BosonogaLine lines[BOSONOGA_LINE_LIMIT + 1];
    unsigned char line_aspect[BOSONOGA_LINE_ASPECT_BYTES];
  };
  union {
    BosonogaToken tokens[BOSONOGA_TOKEN_LIMIT + 1];
    unsigned char token_aspect[BOSONOGA_TOKEN_ASPECT_BYTES];
  };
  union {
    struct {
      BOSONOGA_SIZE line_count;
      BOSONOGA_SIZE token_count;
      BOSONOGA_SIZE variable_count;
      BOSONOGA_SIZE version_count;
      BOSONOGA_SIZE lega_count;
      BOSONOGA_SIZE aka_count;
      BOSONOGA_SIZE main_count;
      BosonogaVersion version[BOSONOGA_VERSION_LIMIT];
      BosonogaLega lega[BOSONOGA_LEGA_LIMIT];
      BosonogaAka aka[BOSONOGA_AKA_LIMIT];
      BosonogaVariable variables[BOSONOGA_VARIABLE_LIMIT];
    };
    unsigned char state_aspect[BOSONOGA_STATE_ASPECT_BYTES];
  };
}
Regina;

_Static_assert(sizeof(BosonogaLine) == BOSONOGA_LINE_LENGTH + 1U,
               "a line must be exactly 256 bytes");
_Static_assert(sizeof(BosonogaToken) == BOSONOGA_TOKEN_LENGTH + 1U,
               "a token must be exactly 64 bytes");
_Static_assert(BOSONOGA_SOURCE_ASPECT_BYTES + BOSONOGA_LINE_ASPECT_BYTES +
                       BOSONOGA_TOKEN_ASPECT_BYTES +
                       BOSONOGA_STATE_ASPECT_BYTES ==
                   BOSONOGA_REGINA_BYTES,
               "the four aspects must add up to exactly 240 MiB");
_Static_assert(sizeof(BosonogaLine[BOSONOGA_LINE_LIMIT + 1]) ==
                   BOSONOGA_LINE_ASPECT_BYTES,
               "lines must fill their 64 MiB aspect exactly");
_Static_assert(sizeof(BosonogaToken[BOSONOGA_TOKEN_LIMIT + 1]) ==
                   BOSONOGA_TOKEN_ASPECT_BYTES,
               "tokens must fill their 112 MiB aspect exactly");
_Static_assert(offsetof(Regina, lines) == BOSONOGA_SOURCE_ASPECT_BYTES,
               "the source aspect must be exactly 16 MiB");
_Static_assert(offsetof(Regina, tokens) ==
                   BOSONOGA_SOURCE_ASPECT_BYTES + BOSONOGA_LINE_ASPECT_BYTES,
               "the line aspect must be exactly 64 MiB");
_Static_assert(offsetof(Regina, line_count) ==
                   BOSONOGA_REGINA_BYTES - BOSONOGA_STATE_ASPECT_BYTES,
               "the token aspect must be exactly 112 MiB");
_Static_assert(offsetof(Regina, variables) - offsetof(Regina, line_count) <=
                   BOSONOGA_STATE_RESERVE_BYTES,
               "counters + version + lega + aka must fit their 1 MiB reserve");
_Static_assert(sizeof(Regina) == BOSONOGA_REGINA_BYTES,
               "Regina must be exactly 240 MiB");

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
int bosonoga_core(Regina* regina, const char* header, const char* input);

#endif
