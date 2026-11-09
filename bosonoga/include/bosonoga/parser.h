#ifndef BOSONOGA_PARSER_H_
#define BOSONOGA_PARSER_H_

#include <bosonoga/settings.h>
#include <glib.h>

// The grammar of a .bosonoga program, in which whitespace only separates tokens
// and // starts a comment that runs to the end of the line:
//
//   program    := "main" block | section+
//   section    := ("gfx_global" | "gfx_init" | "gfx_frame" | "gfx_cleanup"
//                 | "gfx_main") block
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
//   conversion := ("to_f32" | "to_i32" | "to_sin" | "to_cos" | "to_sqrt")
//               value
//               | ("to_atan2" | "to_pow") value value
//   condition  := comparison (("and" | "or") comparison)*
//   comparison := value compare value
//   compare    := "gt" | "lt" | "eq" | "not_eq" | "eq_3" | "eq_6" | "not_eq_3"
//               | "not_eq_6" | "shift_has"
//   value      := INTEGER | REAL | SHIFT | COLOR | GLOBAL | "gfx_delta_time"
//               | NAME
//   INTEGER    := "-"? digit+ "_i32"
//   UNSIGNED   := digit+ "_u32"
//   REAL       := "-"? digit+ ("d" digit+)? "_f32"
//   SHIFT      := "shift_" digit+
//   COLOR      := "color_" KEY
//   GLOBAL     := "global_" KEY
//   LOOP       := "to_" digit+
//
// "and" binds tighter than "or". A variable is visible until the end of its
// block and cannot hide another visible variable. to_subtract subtracts the
// other values from the first, and negates a single value. to_divide divides
// the first value by the others, and inverts a single value, so it is 1 divided
// by that value. Dividing an i32 truncates toward zero, and dividing by zero is
// a runtime error. to_sin, to_cos and to_atan2 work on f32 angles in radians.
// to_atan2 takes y and then x, like atan2 in C, and gives an angle from -pi to
// pi, which is 0 when both are 0. to_sqrt and to_pow work on f32 too. to_pow
// takes the base and then the exponent. The square root of a negative number, a
// negative number to a fractional power, and a result outside the f32 range, as
// from 0 to a negative power, are runtime errors.
//
// A gfx program has one gfx_main and at most one of each other section, in any
// order, except that gfx_global must be the first. A callback, which is any
// section but gfx_main, cannot return. The variables declared at the top of the
// block of gfx_global are visible in all other sections, which share them, so a
// value that one section sets is the value that the others see, also in later
// frames. Other variables belong to their section. A window is open while
// bosonoga_gfx_run runs a gfx program, see gfx.h: gfx_global runs once first,
// then gfx_init once before the first frame, gfx_frame once per frame,
// gfx_cleanup once when the window closes unless gfx_global or gfx_init failed,
// and gfx_main spans frames until it returns, which closes the window. With
// bosonoga_run, as in a test, there is no window and no frames, so gfx_frame
// does not run, and the others run in the order gfx_global, gfx_init,
// gfx_main, gfx_cleanup.
//
// A KEY is any key of the [color] group of the settings, see settings.h, so
// that group decides which COLOR words exist, and a word like color_0 without a
// key is an ordinary name. A COLOR is the value of its key, which is printed as
// RRGGBB, and it can be stored and printed but not computed with. Only
// gfx_frame can have gfx_clear, which sets the color that the frame is cleared
// to before anything is drawn, by default color_background, and draw_rectangle,
// whose f32 values are x, y, width and height in pixels, with the origin in the
// top left corner of the window and y growing down, and whose last value is a
// color. Later rectangles of a frame cover earlier ones.
//
// The KEY of a GLOBAL is a key of the [global] group of the settings in the
// same way, whose value is one INTEGER or REAL, like 100d0_f32, and has its
// type. Only gfx_frame can have gfx_delta_time, the f32 number of seconds since
// the previous frame.

// Where the execution of a program can start.
typedef enum {
  BOSONOGA_SECTION_MAIN,  // main or gfx_main.
  BOSONOGA_SECTION_GFX_INIT,
  BOSONOGA_SECTION_GFX_FRAME,
  BOSONOGA_SECTION_GFX_CLEANUP,
  BOSONOGA_SECTION_GFX_GLOBAL,
  BOSONOGA_SECTION_COUNT,
} BosonogaSection;

// A literal, or the variable in a slot. Either way, literal.kind is its type.
typedef struct {
  BosonogaValue literal;
  gboolean is_variable;
  guint slot;
} BosonogaOperand;

typedef enum {
  BOSONOGA_COMPARE_GT,
  BOSONOGA_COMPARE_LT,
  BOSONOGA_COMPARE_EQ,
  BOSONOGA_COMPARE_NOT_EQ,
  BOSONOGA_COMPARE_EQ_3,
  BOSONOGA_COMPARE_EQ_6,
  BOSONOGA_COMPARE_NOT_EQ_3,
  BOSONOGA_COMPARE_NOT_EQ_6,
  BOSONOGA_COMPARE_SHIFT_HAS,
} BosonogaCompareKind;

typedef enum {
  BOSONOGA_OP_PRINT_VALUE,           // Prints left.
  BOSONOGA_OP_PRINT_STRING,          // Prints text.
  BOSONOGA_OP_PRINT_NEWLINE,         //
  BOSONOGA_OP_ASSIGN,                // slot = left
  BOSONOGA_OP_FLAGS_OR,              // slot |= left
  BOSONOGA_OP_CONVERT_TO_F32,        // slot = left, as f32
  BOSONOGA_OP_CONVERT_TO_I32,        // slot = left, truncated to i32
  BOSONOGA_OP_SIN,                   // slot = sin(left)
  BOSONOGA_OP_COS,                   // slot = cos(left)
  BOSONOGA_OP_ATAN2,                 // slot = atan2(left, right)
  BOSONOGA_OP_SQRT,                  // slot = sqrt(left)
  BOSONOGA_OP_POW,                   // slot = pow(left, right)
  BOSONOGA_OP_ACCUMULATOR_LOAD,      // accumulator = left
  BOSONOGA_OP_ACCUMULATOR_ADD,       // accumulator += left
  BOSONOGA_OP_ACCUMULATOR_SUBTRACT,  // accumulator -= left
  BOSONOGA_OP_ACCUMULATOR_MULTIPLY,  // accumulator *= left
  BOSONOGA_OP_ACCUMULATOR_DIVIDE,    // accumulator /= left
  BOSONOGA_OP_ACCUMULATOR_STORE,     // slot = accumulator
  BOSONOGA_OP_JUMP,                  // Continues at target.
  BOSONOGA_OP_JUMP_UNLESS,           // Continues at target if compare is false.
  BOSONOGA_OP_LOOP_TEST,             // Continues at target if slot >= integer.
  BOSONOGA_OP_LOOP_NEXT,             // ++slot, and continues at target.
  BOSONOGA_OP_SLEEP,                 // Pauses for milliseconds.
  BOSONOGA_OP_RETURN,                // Ends the program with status integer.
  BOSONOGA_OP_MISSING_RETURN,  // Fails: the program ended without a return.
  BOSONOGA_OP_SECTION_END,     // Ends a callback section.
  BOSONOGA_OP_GFX_CLEAR,       // Sets the clear color of the frame to left.
  BOSONOGA_OP_DRAW_RECTANGLE,  // left, right, third, fourth, fifth.
  BOSONOGA_OP_GFX_DELTA_TIME,  // slot = the delta time of the scene.
} BosonogaOpKind;

typedef struct {
  BosonogaOpKind kind;
  BosonogaCompareKind compare;
  gsize line;
  gsize column;
  guint slot;
  guint target;  // Index of an op.
  gint32 integer;
  guint32 milliseconds;
  BosonogaOperand left;
  BosonogaOperand right;
  BosonogaOperand third;   // Only of BOSONOGA_OP_DRAW_RECTANGLE.
  BosonogaOperand fourth;  // Only of BOSONOGA_OP_DRAW_RECTANGLE.
  BosonogaOperand fifth;   // Only of BOSONOGA_OP_DRAW_RECTANGLE.
  const gchar *text;       // Slice of the source, without the quotes.
  gsize length;
} BosonogaOp;

typedef struct {
  const gchar *path;  // Not owned.
  GArray *ops;        // Of BosonogaOp.
  guint slot_count;   // Variables and loop counters.
  gboolean is_gfx;    // Whether the program has a gfx_main.
  // Index in ops where each section starts. A section that the program does not
  // have is a BOSONOGA_OP_SECTION_END, so that it can run and does nothing.
  guint entries[BOSONOGA_SECTION_COUNT];
} BosonogaProgram;

// Returns a new program, or NULL on failure. The program points into path and
// source, which must outlive it. Path is only used in error messages, so it
// must be UTF-8. The settings decide which colors and globals exist, and are
// not used after this returns.
BosonogaProgram *bosonoga_parse(const gchar *path, const gchar *source,
                                gsize length, const BosonogaSettings *settings,
                                GError **error);

void bosonoga_program_free(BosonogaProgram *program);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(BosonogaProgram, bosonoga_program_free)

#endif  // BOSONOGA_PARSER_H_
