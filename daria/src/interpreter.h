#ifndef DARIA_INTERPRETER_H_
#define DARIA_INTERPRETER_H_

#include <glib.h>

#include "parser/parser.h"

typedef struct _DariaExecution DariaExecution;

typedef struct {
  gfloat x;
  gfloat y;
  gfloat width;
  gfloat height;
  guint32 color;  // 0xRRGGBB
} DariaGfxRectangle;

// What the statements of a gfx_frame ask for. The interpreter only records it
// here, and whoever owns the window acts on it.
typedef struct {
  GArray *rectangles;   // Of DariaGfxRectangle, from draw_rectangle.
  guint32 clear_color;  // 0xRRGGBB, set by gfx_clear.
  gfloat delta_time;    // What gfx_delta_time is, set by the window.
} DariaGfxScene;

typedef enum {
  DARIA_EXECUTION_RUNNING,   // Used up its ops and has more to run.
  DARIA_EXECUTION_SLEEPING,  // Is in a sleep_ms statement.
  DARIA_EXECUTION_RETURNED,  // Reached a return or the end of a callback.
  DARIA_EXECUTION_FAILED,    // Stopped with an error.
} DariaExecutionState;

// Returns the zeroed variables of a program, to be freed with g_free. All
// executions of one run of the program share them, so that the variables of
// gfx_global are shared by all sections.
DariaValue *daria_slots_new(const DariaProgram *program);

// Returns a new execution of a section of the program, which must outlive it,
// as must the slots from daria_slots_new. The statements of a gfx_frame record
// in scene, which must also outlive it and can only be NULL for other sections.
// What the info statements print is appended to output or, if output is NULL,
// goes to stdout.
DariaExecution *daria_execution_new(const DariaProgram *program,
                                    DariaSection section, DariaValue *slots,
                                    DariaGfxScene *scene, GString *output);

void daria_execution_free(DariaExecution *execution);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(DariaExecution, daria_execution_free)

// Runs at most op_budget ops without blocking, so that a caller can do other
// work between the calls. While the execution sleeps, no ops are run. The
// status of a return statement is stored in exit_status.
DariaExecutionState daria_execution_advance(DariaExecution *execution,
                                            guint op_budget, gint *exit_status,
                                            GError **error);

// Runs a section of the program until it ends, and blocks while it sleeps.
// Slots, scene and output are the same as in daria_execution_new, and returns
// whether there was no error.
gboolean daria_run_section(const DariaProgram *program, DariaSection section,
                           DariaValue *slots, DariaGfxScene *scene,
                           GString *output, gint *exit_status, GError **error);

// Runs the program from daria_parse without a window, see parser.h. Output is
// the same as in daria_execution_new. Returns whether there was no error, and
// the status of the return statement is stored in exit_status.
gboolean daria_run(const DariaProgram *program, GString *output,
                   gint *exit_status, GError **error);

#endif  // DARIA_INTERPRETER_H_
