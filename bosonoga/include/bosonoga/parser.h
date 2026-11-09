#ifndef BOSONOGA_PARSER_H_
#define BOSONOGA_PARSER_H_

#include <bosonoga/settings.h>
#include <glib.h>

// The grammar of a .bosonoga program. Whitespace separates tokens, and //
// starts a comment that runs to the end of the line.
//
//   program    := item+
//   item       := ("bosonoga_global" | "bosonoga_main") block | section
//                 | function | array
//   section    := ("gfx_init" | "gfx_frame" | "gfx_cleanup" | "gfx_main")
//                 block
//   function   := "fn" NAME PARAMETER* ("ret" INDEXED+)? block
//   array      := "array" NAME INDEXED+ "di" row+ "do"
//   row        := "row" value+
//   block      := "di" statement+ "do"
//   statement  := "info" value+
//               | "var" NAME "is" (initial | call "ret" INDEXED)
//               | "set" NAME update
//               | "set" NAME "to" call "ret" INDEXED
//               | "call" call ("with" "ret" block)?
//               | "if" condition block ("else" "if" condition block)*
//                 ("else" block)?
//               | LOOP ("with" NAME)? block
//               | "while" condition block
//               | "break" | "continue" | "nihil"
//               | "sleep_ms" UNSIGNED
//               | "return" ("exit_success" | "exit_failure" | value*)
//               | "gfx_clear" value
//               | "draw_rectangle" value value value value value
//   initial    := conversion | value ("shift_or" value)*
//   update     := conversion
//               | ("to_add" | "to_subtract" | "to_multiply" | "to_divide"
//                 | "to_mod") value+
//   conversion := ("to_f32" | "to_i32" | "to_sin" | "to_cos" | "to_sqrt")
//               value
//               | ("to_atan2" | "to_pow") value value
//   call       := NAME value*
//   condition  := comparison (("and" | "or") comparison)*
//   comparison := "not"? value compare value
//   compare    := "gt" | "lt" | "ge" | "le" | "eq" | "not_eq" | "eq_3" | "eq_6"
//               | "not_eq_3" | "not_eq_6" | "shift_has"
//   value      := INTEGER | REAL | STRING | SHIFT | COLOR | GLOBAL | RESULT
//               | "gfx_delta_time" | NAME
//   TYPE       := "i32" | "f32" | "flags" | "color" | "str"
//   PARAMETER  := NAME, which ends with "_" TYPE, like count_i32
//   INDEXED    := digit+ "_" TYPE, like 0_i32 and 1_str
//   INTEGER    := "-"? digit+ "_i32"
//   UNSIGNED   := digit+ "_u32"
//   REAL       := "-"? digit+ ("d" digit+)? "_f32"
//   STRING     := '"' character* '"' "_str"
//   SHIFT      := "shift_" digit+
//   COLOR      := "color_" KEY
//   GLOBAL     := "global_" KEY
//   LOOP       := "to_" digit+
//   RESULT     := "ret_" digit+ "_" TYPE, like ret_0_i32
//
// - "and" binds tighter than "or", and "not" negates only the comparison after
//   it. A variable is visible to the end of its block and cannot hide another.
// - to_subtract and to_divide apply the other values to the first, and a single
//   value is negated or inverted. to_mod keeps the remainder, with the sign of
//   the dividend as % and fmod in C, and needs at least two values. An i32
//   division truncates toward zero. Dividing by zero, to_sqrt of a negative
//   number, a negative number to_pow a fraction and an f32 result out of range
//   are runtime errors. Angles are f32 radians, and to_atan2 takes y, then x.
// - break and continue apply to the innermost LOOP or while.
// - A STRING has no escapes, and cannot hold a quote, a backslash, a newline or
//   NUL. A str can be stored, printed, compared with eq and not_eq, and set
//   with to_add and a single str, but not computed with.
// - A program has either one bosonoga_main or the sections of a gfx program,
//   before either of which it can have one bosonoga_global, and any number of
//   functions and arrays. Each can only be used after its definition, so a
//   function can call itself but two cannot call each other.
// - A function sees only its parameters and its own variables, and everything
//   is passed and returned by value. The INDEXED types after ret, and those of
//   an array, are numbered from 0 in order. A return gives one value per type,
//   and the end of the block is a runtime error for a function with ret.
// - A caller names each value that it takes with its index and type. var and
//   set ... to take a call that returns one value, which ends with ret 0_TYPE.
//   Otherwise "call ... with ret" and a block take them as RESULT, those of the
//   innermost call. Calls nest at most BOSONOGA_CALL_DEPTH_MAX deep, and a
//   function cannot have the statements of gfx_frame.
// - An array is a table of literal rows, read like a function of one i32 index
//   from 0, which returns that row. An index without a row is a runtime error.
// - bosonoga_global runs first and cannot return. The variables at the top of
//   its block are shared by bosonoga_main and all sections.
// - A gfx program has one gfx_main and at most one of each other section, in
//   any order. Only gfx_main can return. In a window, see gfx.h, gfx_init runs
//   once, gfx_frame once per frame, gfx_main across frames until it returns,
//   and gfx_cleanup when the window closes. Without a window, gfx_frame never
//   runs.
// - A KEY is a key of the [color] or [global] group of the settings, see
//   settings.h. A color can be stored and printed, and is printed as RRGGBB. A
//   global is one INTEGER or REAL. gfx_clear, draw_rectangle (x, y, width,
//   height, color, with y growing down) and gfx_delta_time (seconds since the
//   previous frame) are only in gfx_frame.

typedef enum {
  BOSONOGA_SECTION_MAIN,
  BOSONOGA_SECTION_GFX_INIT,
  BOSONOGA_SECTION_GFX_FRAME,
  BOSONOGA_SECTION_GFX_CLEANUP,
  BOSONOGA_SECTION_GLOBAL,
  BOSONOGA_SECTION_COUNT,
} BosonogaSection;

// Either way, literal.kind is the type.
typedef struct {
  BosonogaValue literal;
  gboolean is_variable;
  guint slot;
} BosonogaOperand;

typedef enum {
  BOSONOGA_COMPARE_GT,
  BOSONOGA_COMPARE_LT,
  BOSONOGA_COMPARE_GE,
  BOSONOGA_COMPARE_LE,
  BOSONOGA_COMPARE_EQ,
  BOSONOGA_COMPARE_NOT_EQ,
  BOSONOGA_COMPARE_EQ_3,
  BOSONOGA_COMPARE_EQ_6,
  BOSONOGA_COMPARE_NOT_EQ_3,
  BOSONOGA_COMPARE_NOT_EQ_6,
  BOSONOGA_COMPARE_SHIFT_HAS,
} BosonogaCompareKind;

typedef enum {
  BOSONOGA_OP_PRINT_VALUE,
  BOSONOGA_OP_PRINT_NEWLINE,
  BOSONOGA_OP_ASSIGN,
  BOSONOGA_OP_FLAGS_OR,
  BOSONOGA_OP_CONVERT_TO_F32,
  BOSONOGA_OP_CONVERT_TO_I32,
  BOSONOGA_OP_SIN,
  BOSONOGA_OP_COS,
  BOSONOGA_OP_ATAN2,
  BOSONOGA_OP_SQRT,
  BOSONOGA_OP_POW,
  BOSONOGA_OP_ACCUMULATOR_LOAD,
  BOSONOGA_OP_ACCUMULATOR_ADD,
  BOSONOGA_OP_ACCUMULATOR_SUBTRACT,
  BOSONOGA_OP_ACCUMULATOR_MULTIPLY,
  BOSONOGA_OP_ACCUMULATOR_DIVIDE,
  BOSONOGA_OP_ACCUMULATOR_MOD,
  BOSONOGA_OP_ACCUMULATOR_STORE,
  BOSONOGA_OP_JUMP,
  BOSONOGA_OP_JUMP_UNLESS,  // Jumps if compare, negated if negate, is false.
  BOSONOGA_OP_LOOP_TEST,
  BOSONOGA_OP_LOOP_NEXT,
  BOSONOGA_OP_SLEEP,
  BOSONOGA_OP_RETURN,
  BOSONOGA_OP_MISSING_RETURN,
  BOSONOGA_OP_SECTION_END,
  BOSONOGA_OP_GFX_CLEAR,
  BOSONOGA_OP_DRAW_RECTANGLE,
  BOSONOGA_OP_GFX_DELTA_TIME,
  BOSONOGA_OP_ARGUMENT,
  BOSONOGA_OP_CALL,
  BOSONOGA_OP_RESULT,
  BOSONOGA_OP_FUNCTION_RETURN,
  BOSONOGA_OP_TAKE_RESULT,
  BOSONOGA_OP_ARRAY_READ,
} BosonogaOpKind;

typedef struct {
  BosonogaOpKind kind;
  BosonogaCompareKind compare;
  gboolean negate;
  gsize line;
  gsize column;
  guint slot;
  guint target;
  guint function;
  guint index;
  gint32 integer;
  guint32 milliseconds;
  BosonogaOperand left;
  BosonogaOperand right;
  BosonogaOperand third;  // The last three are only of draw_rectangle.
  BosonogaOperand fourth;
  BosonogaOperand fifth;
} BosonogaOp;

// The most values that a function takes or returns, and of a row.
#define BOSONOGA_FUNCTION_VALUES_MAX 16
#define BOSONOGA_CALL_DEPTH_MAX 10000

// An array is read like a function of its index that returns a row.
typedef struct {
  const gchar *name;  // Slice of the source.
  gsize name_length;
  gboolean is_array;
  guint entry;
  guint parameter_count;
  guint result_count;
  BosonogaValueKind parameters[BOSONOGA_FUNCTION_VALUES_MAX];
  BosonogaValueKind results[BOSONOGA_FUNCTION_VALUES_MAX];
  // Of its own frame, parameters first. Not of an array.
  guint slot_count;
  guint row_count;    // Only of an array.
  guint first_value;  // Only of an array, index in program->rows of row 0.
} BosonogaFunction;

typedef struct {
  const gchar *path;  // Not owned.
  GArray *ops;
  guint slot_count;  // Not of functions.
  GArray *functions;
  GArray *rows;  // Of all arrays, one row after another.
  gboolean is_gfx;
  // A missing section starts at a BOSONOGA_OP_SECTION_END, so it can still run.
  guint entries[BOSONOGA_SECTION_COUNT];
} BosonogaProgram;

// The program points into path and source, which must outlive it. Path is
// UTF-8, for error messages. The settings are not used after this returns.
BosonogaProgram *bosonoga_parse(const gchar *path, const gchar *source,
                                gsize length, const BosonogaSettings *settings,
                                GError **error);

void bosonoga_program_free(BosonogaProgram *program);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(BosonogaProgram, bosonoga_program_free)

#endif  // BOSONOGA_PARSER_H_
