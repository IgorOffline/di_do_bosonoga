#ifndef DARIA_INTERPRETER_H_
#define DARIA_INTERPRETER_H_

#include <glib.h>

#include "parser.h"

// Runs program from daria_parse and appends what its info statements print to
// output. Returns whether the program reached a return statement, whose status
// is stored in exit_status.
gboolean daria_run(const DariaProgram *program, GString *output,
                   gint *exit_status, GError **error);

#endif  // DARIA_INTERPRETER_H_
