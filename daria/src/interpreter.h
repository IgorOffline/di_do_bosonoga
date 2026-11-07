#ifndef DARIA_INTERPRETER_H_
#define DARIA_INTERPRETER_H_

#include <glib.h>

#include "parser/parser.h"

typedef struct _DariaExecution DariaExecution;

typedef enum {
  DARIA_EXECUTION_RUNNING,   // Used up its ops and has more to run.
  DARIA_EXECUTION_SLEEPING,  // Is in a sleep_ms statement.
  DARIA_EXECUTION_RETURNED,  // Reached a return or the end of a callback.
  DARIA_EXECUTION_FAILED,    // Stopped with an error.
} DariaExecutionState;

// Returns a new execution of a section of the program, which must outlive it.
// What the info statements print is appended to output or, if output is NULL,
// goes to stdout.
DariaExecution *daria_execution_new(const DariaProgram *program,
                                    DariaSection section, GString *output);

void daria_execution_free(DariaExecution *execution);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(DariaExecution, daria_execution_free)

// Runs at most op_budget ops without blocking, so that a caller can do other
// work between the calls. While the execution sleeps, no ops are run. The
// status of a return statement is stored in exit_status.
DariaExecutionState daria_execution_advance(DariaExecution *execution,
                                            guint op_budget, gint *exit_status,
                                            GError **error);

// Runs a section of the program until it ends, and blocks while it sleeps.
// Output is the same as in daria_execution_new, and returns whether there was
// no error.
gboolean daria_run_section(const DariaProgram *program, DariaSection section,
                           GString *output, gint *exit_status, GError **error);

// Runs the program from daria_parse without a window, see parser.h. Output is
// the same as in daria_execution_new. Returns whether there was no error, and
// the status of the return statement is stored in exit_status.
gboolean daria_run(const DariaProgram *program, GString *output,
                   gint *exit_status, GError **error);

#endif  // DARIA_INTERPRETER_H_
