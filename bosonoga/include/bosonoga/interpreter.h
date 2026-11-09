#ifndef BOSONOGA_INTERPRETER_H_
#define BOSONOGA_INTERPRETER_H_

#include <bosonoga/parser.h>
#include <glib.h>

typedef struct _BosonogaExecution BosonogaExecution;

typedef struct {
  gfloat x;
  gfloat y;
  gfloat width;
  gfloat height;
  guint32 color;
} BosonogaGfxRectangle;

// What a gfx_frame records, for the owner of the window to draw.
typedef struct {
  GArray *rectangles;
  guint32 clear_color;
  gfloat delta_time;
} BosonogaGfxScene;

typedef enum {
  BOSONOGA_EXECUTION_RUNNING,
  BOSONOGA_EXECUTION_SLEEPING,
  BOSONOGA_EXECUTION_RETURNED,
  BOSONOGA_EXECUTION_FAILED,
} BosonogaExecutionState;

// Freed with g_free. All executions of one run share them, for bosonoga_global.
BosonogaValue *bosonoga_slots_new(const BosonogaProgram *program);

// Program, slots and scene must outlive the execution. Scene can only be NULL
// outside gfx_frame. info appends to output, or prints to stdout if it is NULL.
BosonogaExecution *bosonoga_execution_new(const BosonogaProgram *program,
                                          BosonogaSection section,
                                          BosonogaValue *slots,
                                          BosonogaGfxScene *scene,
                                          GString *output);

void bosonoga_execution_free(BosonogaExecution *execution);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(BosonogaExecution, bosonoga_execution_free)

// Runs at most op_budget ops, and none while asleep, without blocking.
BosonogaExecutionState bosonoga_execution_advance(BosonogaExecution *execution,
                                                  guint op_budget,
                                                  gint *exit_status,
                                                  GError **error);

gboolean bosonoga_run_section(const BosonogaProgram *program,
                              BosonogaSection section, BosonogaValue *slots,
                              BosonogaGfxScene *scene, GString *output,
                              gint *exit_status, GError **error);

gboolean bosonoga_run(const BosonogaProgram *program, GString *output,
                      gint *exit_status, GError **error);

#endif  // BOSONOGA_INTERPRETER_H_
