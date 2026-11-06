#ifndef DARIA_INTERPRETER_H_
#define DARIA_INTERPRETER_H_

#include <glib.h>

// Runs program, an array of DariaOp from daria_parse, and writes its output to
// stdout.
gboolean daria_run(const GArray *program, GError **error);

#endif  // DARIA_INTERPRETER_H_
