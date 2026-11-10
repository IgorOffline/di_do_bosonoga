#ifndef BOSONOGA_EXECUTION_H_
#define BOSONOGA_EXECUTION_H_

#include <glib.h>

#include "arithmetic.h"
#include "bosonoga/interpreter.h"

typedef struct {
  guint function;
  guint return_to;
  guint base;  // Of its frame in the stack.
} BosonogaCall;

struct _BosonogaExecution {
  const BosonogaProgram *program;
  BosonogaValue *slots;
  BosonogaAccumulator accumulator;
  guint next;
  gint64 wake_time;
  BosonogaGfxScene *scene;
  GString *output;
  gboolean to_stdout;  // Then output is an owned buffer for stdout.
  // A frame per call, with copies of its arguments.
  GArray *stack;
  GArray *calls;  // Of BosonogaCall, innermost last.
  BosonogaValue arguments[BOSONOGA_FUNCTION_VALUES_MAX];
  BosonogaValue results[BOSONOGA_FUNCTION_VALUES_MAX];
};

BosonogaExecutionState bosonoga_execute(BosonogaExecution *execution,
                                        guint op_budget, gint *exit_status,
                                        GError **error);

#endif  // BOSONOGA_EXECUTION_H_
