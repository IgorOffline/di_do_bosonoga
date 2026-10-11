#ifndef BOSONOGA_H
#define BOSONOGA_H

#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN "bosonoga"
#endif
#ifndef GLIB_VERSION_MIN_REQUIRED
#define GLIB_VERSION_MIN_REQUIRED GLIB_VERSION_2_84
#endif
#ifndef GLIB_VERSION_MAX_ALLOWED
#define GLIB_VERSION_MAX_ALLOWED GLIB_VERSION_2_84
#endif

#include <glib.h>

G_BEGIN_DECLS

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

#define BOSONOGA_ERROR (bosonoga_error_quark())

typedef enum {
  BOSONOGA_ERROR_SOURCE,
  BOSONOGA_ERROR_LIMIT,
  BOSONOGA_ERROR_SYNTAX,
  BOSONOGA_ERROR_NAME,
  BOSONOGA_ERROR_TYPE,
  BOSONOGA_ERROR_RANGE,
  BOSONOGA_ERROR_RUNTIME
} BosonogaError;

typedef enum {
  BOSONOGA_KIND_I32,
  BOSONOGA_KIND_F32,
  BOSONOGA_KIND_FLAGS
} BosonogaKind;

typedef struct {
  gchar data[BOSONOGA_LINE_LENGTH + 1];
} BosonogaLine;

typedef struct {
  gchar data[BOSONOGA_TOKEN_LENGTH + 1];
} BosonogaToken;

typedef struct {
  gchar string[BOSONOGA_TOKEN_LENGTH + 1];
  gsize len;
} BosonogaVersion;

typedef struct {
  gchar legal[BOSONOGA_TOKEN_LENGTH + 1];
  gsize len;
} BosonogaLega;

typedef struct {
  gchar original[BOSONOGA_TOKEN_LENGTH + 1];
  gchar replacement[BOSONOGA_TOKEN_LENGTH + 1];
  gsize original_len;
  gsize replacement_len;
} BosonogaAka;

typedef struct {
  BosonogaKind kind;
  gint32 i32;
  gfloat f32;
  guint32 flags;
} BosonogaValue;

typedef struct {
  gchar name[BOSONOGA_TOKEN_LENGTH + 1];
  BosonogaValue value;
} BosonogaVariable;

typedef struct {
  union {
    struct {
      gchar header_source[BOSONOGA_SOURCE_LENGTH + 1];
      gchar input_source[BOSONOGA_SOURCE_LENGTH + 1];
    };
    guint8 source_aspect[BOSONOGA_SOURCE_ASPECT_BYTES];
  };
  union {
    BosonogaLine lines[BOSONOGA_LINE_LIMIT + 1];
    guint8 line_aspect[BOSONOGA_LINE_ASPECT_BYTES];
  };
  union {
    BosonogaToken tokens[BOSONOGA_TOKEN_LIMIT + 1];
    guint8 token_aspect[BOSONOGA_TOKEN_ASPECT_BYTES];
  };
  union {
    struct {
      gsize line_count;
      gsize header_line_count;
      gsize token_count;
      gsize variable_count;
      gsize version_count;
      gsize lega_count;
      gsize aka_count;
      gsize main_count;
      gboolean returned;
      gint exit_status;
      BosonogaVersion version[BOSONOGA_VERSION_LIMIT];
      BosonogaLega lega[BOSONOGA_LEGA_LIMIT];
      BosonogaAka aka[BOSONOGA_AKA_LIMIT];
      BosonogaVariable variables[BOSONOGA_VARIABLE_LIMIT];
    };
    guint8 state_aspect[BOSONOGA_STATE_ASPECT_BYTES];
  };
} Regina;

G_STATIC_ASSERT(BOSONOGA_SOURCE_ASPECT_BYTES + BOSONOGA_LINE_ASPECT_BYTES +
                    BOSONOGA_TOKEN_ASPECT_BYTES + BOSONOGA_STATE_ASPECT_BYTES ==
                BOSONOGA_REGINA_BYTES);
G_STATIC_ASSERT(sizeof(BosonogaLine) == BOSONOGA_LINE_LENGTH + 1U);
G_STATIC_ASSERT(sizeof(BosonogaToken) == BOSONOGA_TOKEN_LENGTH + 1U);
G_STATIC_ASSERT(sizeof(BosonogaLine[BOSONOGA_LINE_LIMIT + 1]) ==
                BOSONOGA_LINE_ASPECT_BYTES);
G_STATIC_ASSERT(sizeof(BosonogaToken[BOSONOGA_TOKEN_LIMIT + 1]) ==
                BOSONOGA_TOKEN_ASPECT_BYTES);
G_STATIC_ASSERT((gsize)G_STRUCT_OFFSET(Regina, lines) ==
                BOSONOGA_SOURCE_ASPECT_BYTES);
G_STATIC_ASSERT((gsize)G_STRUCT_OFFSET(Regina, tokens) ==
                BOSONOGA_SOURCE_ASPECT_BYTES + BOSONOGA_LINE_ASPECT_BYTES);
G_STATIC_ASSERT((gsize)G_STRUCT_OFFSET(Regina, line_count) ==
                BOSONOGA_REGINA_BYTES - BOSONOGA_STATE_ASPECT_BYTES);
G_STATIC_ASSERT((gsize)(G_STRUCT_OFFSET(Regina, variables) -
                        G_STRUCT_OFFSET(Regina, line_count)) <=
                BOSONOGA_STATE_RESERVE_BYTES);
G_STATIC_ASSERT(sizeof(Regina) == BOSONOGA_REGINA_BYTES);

static inline const gchar* token_at(const Regina* regina, gsize position) {
  return regina->tokens[position].data;
}

static inline gsize tokens_left(const Regina* regina, gsize position) {
  return regina->token_count - position;
}

GQuark bosonoga_error_quark(void);

gboolean bosonoga_read_source(const gchar* path,
                              gchar destination[BOSONOGA_SOURCE_LENGTH + 1],
                              GError** error);

gboolean bosonoga_load_program(Regina* regina, const gchar* header,
                               const gchar* input, GError** error);

gboolean bosonoga_fail_at(const Regina* regina, gsize position, GError** error,
                          BosonogaError code, const gchar* format, ...)
    G_GNUC_PRINTF(5, 6);

gboolean bosonoga_core(Regina* regina, const gchar* header, const gchar* input,
                       gint* exit_status, GError** error);

G_END_DECLS

#endif
