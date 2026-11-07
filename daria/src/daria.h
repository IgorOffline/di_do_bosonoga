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

// The keys of the [color] group of regina.ini, which a program refers to by the
// word of daria_color_word.
typedef enum {
  DARIA_COLOR_BACKGROUND,
  DARIA_COLOR_RECTANGLE,
  DARIA_COLOR_COUNT,
} DariaColorName;

GQuark daria_error_quark(void);

const gchar *daria_color_word(DariaColorName color);

#endif  // DARIA_DARIA_H_
