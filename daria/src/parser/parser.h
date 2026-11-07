#ifndef DARIA_PARSER_H_
#define DARIA_PARSER_H_

#include <glib.h>

#include "settings.h"

// The grammar of a .bosonoga program, in which whitespace only separates tokens
// and // starts a comment that runs to the end of the line:
//
//   program    := "main" block | section+
//   section    := ("gfx_init" | "gfx_frame" | "gfx_cleanup" | "gfx_main") block
//   block      := "di" statement+ "do"
//   statement  := "info" argument+
//               | "var" NAME "is" initial
//               | "set" NAME update
//               | "if" condition block ("else" "if" condition block)*
//                 ("else" block)?
//               | LOOP ("with" NAME)? block
//               | "nihil"
//               | "sleep_ms" UNSIGNED
//               | "return" ("exit_success" | "exit_failure")
//               | "gfx_clear" value
//               | "draw_rectangle" value value value value value
//   argument   := value | STRING
//   initial    := conversion | value ("shift_or" value)*
//   update     := conversion
//               | ("to_add" | "to_subtract" | "to_multiply" | "to_divide")
//               value+
//   conversion := ("to_f32" | "to_i32") value
//   condition  := comparison (("and" | "or") comparison)*
//   comparison := value compare value
//   compare    := "gt" | "lt" | "eq" | "not_eq" | "eq_3" | "eq_6" | "not_eq_3"
//               | "not_eq_6" | "shift_has"
//   value      := INTEGER | REAL | SHIFT | COLOR | NAME
//   INTEGER    := "-"? digit+ "_i32"
//   UNSIGNED   := digit+ "_u32"
//   REAL       := "-"? digit+ ("d" digit+)? "_f32"
//   SHIFT      := "shift_" digit+
//   COLOR      := "color_" KEY
//   LOOP       := "to_" digit+
//
// "and" binds tighter than "or". A variable is visible until the end of its
// block and cannot hide another visible variable. to_subtract subtracts the
// other values from the first, and negates a single value. to_divide divides
// the first value by the others, and inverts a single value, so it is 1 divided
// by that value. Dividing an i32 truncates toward zero, and dividing by zero is
// a runtime error.
//
// A gfx program has one gfx_main and at most one of each other section, in any
// order. Sections share no variables, and a callback cannot return. A window is
// open while main.bosonoga runs a gfx program: gfx_init runs once before the
// first frame, gfx_frame once per frame, gfx_cleanup once when the window
// closes unless gfx_init failed, and gfx_main spans frames until it returns,
// which closes the window. As a test there is no window and no frames, so
// gfx_frame does not run, and the others run in the order gfx_init, gfx_main,
// gfx_cleanup.
//
// A KEY is any key of the [color] group of regina.ini, so that group decides
// which COLOR words exist, and a word like color_0 without a key is an ordinary
// name. A COLOR is the value of its key, which is printed as RRGGBB, and it can
// be stored and printed but not computed with. Only gfx_frame can have
// gfx_clear, which sets the color that the frame is cleared to before anything
// is drawn, by default color_background, and draw_rectangle, whose f32 values
// are x, y, width and height in pixels, with the origin in the top left corner
// of the window and y growing down, and whose last value is a color. Later
// rectangles of a frame cover earlier ones.

// Where the execution of a program can start.
typedef enum {
  DARIA_SECTION_MAIN,  // main or gfx_main.
  DARIA_SECTION_GFX_INIT,
  DARIA_SECTION_GFX_FRAME,
  DARIA_SECTION_GFX_CLEANUP,
  DARIA_SECTION_COUNT,
} DariaSection;

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

// A literal, or the variable in a slot. Either way, literal.kind is its type.
typedef struct {
  DariaValue literal;
  gboolean is_variable;
  guint slot;
} DariaOperand;

typedef enum {
  DARIA_COMPARE_GT,
  DARIA_COMPARE_LT,
  DARIA_COMPARE_EQ,
  DARIA_COMPARE_NOT_EQ,
  DARIA_COMPARE_EQ_3,
  DARIA_COMPARE_EQ_6,
  DARIA_COMPARE_NOT_EQ_3,
  DARIA_COMPARE_NOT_EQ_6,
  DARIA_COMPARE_SHIFT_HAS,
} DariaCompareKind;

typedef enum {
  DARIA_OP_PRINT_VALUE,           // Prints left.
  DARIA_OP_PRINT_STRING,          // Prints text.
  DARIA_OP_PRINT_NEWLINE,         //
  DARIA_OP_ASSIGN,                // slot = left
  DARIA_OP_FLAGS_OR,              // slot |= left
  DARIA_OP_CONVERT_TO_F32,        // slot = left, as f32
  DARIA_OP_CONVERT_TO_I32,        // slot = left, truncated to i32
  DARIA_OP_ACCUMULATOR_LOAD,      // accumulator = left
  DARIA_OP_ACCUMULATOR_ADD,       // accumulator += left
  DARIA_OP_ACCUMULATOR_SUBTRACT,  // accumulator -= left
  DARIA_OP_ACCUMULATOR_MULTIPLY,  // accumulator *= left
  DARIA_OP_ACCUMULATOR_DIVIDE,    // accumulator /= left
  DARIA_OP_ACCUMULATOR_STORE,     // slot = accumulator
  DARIA_OP_JUMP,                  // Continues at target.
  DARIA_OP_JUMP_UNLESS,           // Continues at target if compare is false.
  DARIA_OP_LOOP_TEST,             // Continues at target if slot >= integer.
  DARIA_OP_LOOP_NEXT,             // ++slot, and continues at target.
  DARIA_OP_SLEEP,                 // Pauses for milliseconds.
  DARIA_OP_RETURN,                // Ends the program with status integer.
  DARIA_OP_MISSING_RETURN,        // Fails: the program ended without a return.
  DARIA_OP_SECTION_END,           // Ends a callback section.
  DARIA_OP_GFX_CLEAR,             // Sets the clear color of the frame to left.
  DARIA_OP_DRAW_RECTANGLE,        // left, right, third, fourth, fifth.
} DariaOpKind;

typedef struct {
  DariaOpKind kind;
  DariaCompareKind compare;
  gsize line;
  gsize column;
  guint slot;
  guint target;  // Index of an op.
  gint32 integer;
  guint32 milliseconds;
  DariaOperand left;
  DariaOperand right;
  DariaOperand third;   // Only of DARIA_OP_DRAW_RECTANGLE.
  DariaOperand fourth;  // Only of DARIA_OP_DRAW_RECTANGLE.
  DariaOperand fifth;   // Only of DARIA_OP_DRAW_RECTANGLE.
  const gchar *text;    // Slice of the source, without the quotes.
  gsize length;
} DariaOp;

typedef struct {
  const gchar *path;  // Not owned.
  GArray *ops;        // Of DariaOp.
  guint slot_count;   // Variables and loop counters.
  gboolean is_gfx;    // Whether the program has a gfx_main.
  // Index in ops where each section starts. A section that the program does not
  // have is a DARIA_OP_SECTION_END, so that it can run and does nothing.
  guint entries[DARIA_SECTION_COUNT];
} DariaProgram;

// Returns a new program, or NULL on failure. The program points into path and
// source, which must outlive it. Path is only used in error messages, so it
// must be UTF-8. The settings decide which colors exist, and are not used after
// this returns.
DariaProgram *daria_parse(const gchar *path, const gchar *source, gsize length,
                          const DariaSettings *settings, GError **error);

void daria_program_free(DariaProgram *program);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(DariaProgram, daria_program_free)

#endif  // DARIA_PARSER_H_
