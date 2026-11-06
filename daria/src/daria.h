#ifndef DARIA_DARIA_H_
#define DARIA_DARIA_H_

#include <glib.h>

#define DARIA_EXIT_SUCCESS 0
#define DARIA_EXIT_FAILURE 1

#define DARIA_ERROR (daria_error_quark())

typedef enum {
  DARIA_ERROR_SYNTAX,
} DariaError;

GQuark daria_error_quark(void);

#endif  // DARIA_DARIA_H_
