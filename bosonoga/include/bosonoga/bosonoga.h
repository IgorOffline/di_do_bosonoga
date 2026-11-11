#ifndef BOSONOGA_BOSONOGA_H_
#define BOSONOGA_BOSONOGA_H_

#include <glib.h>

#define BOSONOGA_EXIT_SUCCESS 0
#define BOSONOGA_EXIT_FAILURE 1

#define BOSONOGA_ERROR (bosonoga_error_quark())

typedef enum {
  BOSONOGA_ERROR_SYNTAX,
  BOSONOGA_ERROR_NAME,
  BOSONOGA_ERROR_TYPE,
  BOSONOGA_ERROR_RUNTIME,
  BOSONOGA_ERROR_TEST,
} BosonogaError;

typedef enum {
  BOSONOGA_VALUE_I32,
  BOSONOGA_VALUE_I64,
  BOSONOGA_VALUE_U32,
  BOSONOGA_VALUE_U64,
  BOSONOGA_VALUE_F32,
  BOSONOGA_VALUE_F64,
  BOSONOGA_VALUE_FLAGS,
  BOSONOGA_VALUE_COLOR,
  BOSONOGA_VALUE_STR,
  BOSONOGA_VALUE_BOOL,
  BOSONOGA_VALUE_INPUT_KEY,
  BOSONOGA_VALUE_FONT,
} BosonogaValueKind;

typedef struct {
  BosonogaValueKind kind;
  union {
    gint32 i32;
    gint64 i64;
    guint32 u32;
    guint64 u64;
    gfloat f32;
    gdouble f64;
    guint32 flags;
    guint32 color;  // 0xRRGGBB
    gboolean boolean;
    guint32 input_key;  // As sokol_app numbers keys.
    struct {
      guint index;
      const gchar *text;  // As [font] has it, which the program owns.
    } font;
    struct {
      // Not NUL-terminated. Of the source, or of what new made, which the
      // execution owns.
      const gchar *text;
      gsize length;
    } str;
  };
} BosonogaValue;

GQuark bosonoga_error_quark(void);

#endif  // BOSONOGA_BOSONOGA_H_
