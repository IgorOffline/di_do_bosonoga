#ifndef DARIA_DARIA_H_
#define DARIA_DARIA_H_

#include <glib.h>

#define DARIA_EXIT_SUCCESS 0
#define DARIA_EXIT_FAILURE 1

#define DARIA_ERROR (daria_error_quark())

typedef enum {
  DARIA_ERROR_SYNTAX,
  DARIA_ERROR_NAME,
  DARIA_ERROR_TYPE,
  DARIA_ERROR_RUNTIME,
  DARIA_ERROR_TEST,
} DariaError;

typedef enum {
  DARIA_VALUE_I32,
  DARIA_VALUE_F32,
  DARIA_VALUE_FLAGS,
  DARIA_VALUE_COLOR,
} DariaValueKind;

typedef struct {
  DariaValueKind kind;
  union {
    gint32 i32;
    gfloat f32;
    guint32 flags;
    guint32 color;  // 0xRRGGBB
  };
} DariaValue;

GQuark daria_error_quark(void);

#endif  // DARIA_DARIA_H_
