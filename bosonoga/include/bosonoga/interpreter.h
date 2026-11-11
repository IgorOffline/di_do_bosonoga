#ifndef BOSONOGA_INTERPRETER_H_
#define BOSONOGA_INTERPRETER_H_

#include <bosonoga/parser.h>
#include <glib.h>

typedef struct _BosonogaExecution BosonogaExecution;

typedef enum {
  BOSONOGA_GFX_RECTANGLE,
  BOSONOGA_GFX_HEX,
  BOSONOGA_GFX_CIRCLE,
  BOSONOGA_GFX_TEXT,
} BosonogaGfxShapeKind;

// In pixels, with y growing down. A hex is regular and flat-top, with its
// first corner to the right of its center.
typedef struct {
  BosonogaGfxShapeKind kind;
  // The left of a rectangle, the center of a hex or circle, and of a text as
  // its align says.
  gfloat x;
  gfloat y;  // The top of a rectangle, the center of a hex or circle.
  gfloat width;
  gfloat height;
  gfloat radius;  // Of a circle, or of a hex from its center to a corner.
  guint32 color;
  gfloat size;    // Of a text, from its ascender to its descender.
  guint font;     // Of a text, in the fonts of the program.
  guint32 align;  // Of a text, BosonogaGfxAlign.
  gsize text;     // Of a text, its offset in the text of the scene.
  gsize text_length;
} BosonogaGfxShape;

// What a gfx_frame records, for the owner of the window to draw in order.
typedef struct {
  GArray *shapes;
  GString *text;  // Of all texts, one after another.
  guint32 clear_color;
  gfloat delta_time;
  guint32 key;  // The key that gfx_key_down runs for.
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

// Runs bosonoga_global and bosonoga_main. Rejects window programs.
gboolean bosonoga_run(const BosonogaProgram *program, GString *output,
                      gint *exit_status, GError **error);

#endif  // BOSONOGA_INTERPRETER_H_
