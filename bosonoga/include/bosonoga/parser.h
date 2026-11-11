#ifndef BOSONOGA_PARSER_H_
#define BOSONOGA_PARSER_H_

#include <bosonoga/settings.h>
#include <glib.h>

// The grammar of a .bosonoga program. Whitespace separates tokens, and //
// starts a comment that runs to the end of the line.
//
//   program    := item+
//   item       := ("bosonoga_global" | "bosonoga_main") block | section
//                 | function | array | "const" NAME "is" value
//   section    := ("gfx_init" | "gfx_frame" | "gfx_cleanup" | "gfx_key_down"
//                 | "gfx_main") block
//   function   := "def" FUNCTION PARAMETER* ("ret" INDEXED+ | "ret_void")
//                 block
//   array      := "array" NAME INDEXED+ "di" row+ "do"
//   row        := "row" value+
//   block      := "di" statement+ "do"
//   statement  := "info" value+
//               | ("var" | "val") NAME "is"
//                 (initial | call "with" "ret" INDEXED | new)
//               | "unset" NAME
//               | "set" NAME update
//               | "set" NAME "to" (value | call "with" "ret" INDEXED)
//               | "call" call ("with" "ret" taken+ block)?
//               | "if" condition block ("else" "if" condition block)*
//                 ("else" block)?
//               | LOOP ("with" NAME)? block
//               | "while" condition block
//               | "break" | "continue" | "nihil"
//               | "sleep_ms" value
//               | "return" ("exit_success" | "exit_failure" | value*)
//               | "gfx_clear" value
//               | "gfx_draw_rectangle" value value value value value
//               | "gfx_draw_hex" value value value value
//               | "gfx_draw_circle" value value value value
//   initial    := conversion | value ("shift_or" value)*
//   new        := "new" "str" "of" value+
//   update     := conversion
//               | ("to_add" | "to_subtract" | "to_multiply" | "to_divide"
//                 | "to_mod") value+
//   conversion := ("to_i32" | "to_i64" | "to_u32" | "to_u64" | "to_f32"
//                 | "to_f64") value
//               | ("math_to_sin" | "math_to_cos" | "math_to_sqrt") value
//               | ("math_to_atan2" | "math_to_pow") value value
//               | "math_to_hypot" value value
//               | "math_to_abs" value
//               | ("math_to_min" | "math_to_max") value value
//               | ("math_to_floor" | "math_to_ceil" | "math_to_round") value
//               | ("math_to_log" | "math_to_log10" | "math_to_exp"
//                 | "math_to_asin" | "math_to_acos" | "math_to_tan") value
//               | ("math_to_log1p" | "math_to_expm1") value
//   taken      := INDEXED "as" NAME
//   call       := FUNCTION ("of" value+)? | NAME "at" value
//   condition  := comparison (("and" | "or") comparison)*
//   comparison := "not"? value (compare value)?
//   compare    := "gt" | "lt" | "ge" | "le" | "eq" | "not_eq" | "eq_3" | "eq_6"
//               | "not_eq_3" | "not_eq_6" | "shift_has"
//   value      := INTEGER | REAL | STRING | BOOL | SHIFT | COLOR | GLOBAL
//               | KEY_VALUE | "gfx_delta_time" | "gfx_key" | NAME
//   TYPE       := "i32" | "i64" | "u32" | "u64" | "f32" | "f64" | "flags"
//               | "color" | "str" | "bool" | "input_key"
//   FUNCTION   := NAME "_fn", like divide_fn, which no other name is
//   PARAMETER  := NAME, which ends with "_" TYPE, like count_i32
//   INDEXED    := digit+ "_" TYPE, like 0_i32 and 1_str
//   INTEGER    := "-"? digit+ ("_i32" | "_i64" | "_u32" | "_u64")
//   REAL       := "-"? digit+ ("d" digit+)? ("_f32" | "_f64")
//   STRING     := '"' character* '"' "_str"
//   BOOL       := "true_bool" | "false_bool"
//   SHIFT      := "shift_" digit+
//   COLOR      := "color_" KEY
//   GLOBAL     := "global_" KEY
//   KEY_VALUE  := "key_" KEY
//   LOOP       := "to_" digit+
//
// - "and" binds tighter than "or", and "not" negates only the comparison after
//   it. A comparison without compare is of a bool, and is true if the bool
//   is, so "if done" is "if done eq true_bool". A variable is visible to the
//   end of its block and cannot hide another.
// - A val is a variable that is never set after its declaration. Unlike a
//   const, it can hold a runtime value, as in val half is to_f64 count.
// - set ... to gives a variable a single value of its type, or a call's.
// - to_subtract and to_divide apply the other values to the first, and a single
//   value is negated or inverted. to_mod keeps the remainder, with the sign of
//   the dividend as % and fmod in C, and needs at least two values. Integer
//   division truncates toward zero. The values in between are exact while
//   they fit in a signed 128-bit integer, and only the stored result has to fit
//   in its type: to_add 9223372036854775807_i64 1_i64 -1_i64 gives the largest
//   i64, and to_subtract 2_u32 3_u32 is a runtime error. Dividing by zero,
//   math_to_sqrt of a negative number, a negative number math_to_pow a fraction
//   and a numeric result out of range are runtime errors. Angles are real
//   radians, and math_to_atan2 takes y, then x.
// - math_to_abs preserves the type of one number, and gives a u32 or u64 as it
//   is. The absolute value of the smallest i32 or i64, like -2147483648_i32, is
//   outside its type and is a runtime error. math_to_min and math_to_max take
//   exactly two numbers of the same type and preserve that type.
// - math_to_floor, math_to_ceil and math_to_round preserve f32 or f64. Floor
//   rounds toward negative infinity, ceil toward positive infinity, and round
//   to the nearest whole number, with halves away from zero: -2d5_f32 becomes
//   -3_f32.
//   Use to_i32 separately when an integer result is needed. These operations
//   round stored binary values; they do not provide decimal arithmetic.
// - to_i32, to_i64, to_u32, to_u64, to_f32 and to_f64 accept any numeric type,
//   including the target type. Conversions are explicit; arithmetic,
//   comparisons and binary math functions require matching types. A
//   conversion to an integer first truncates toward zero, and then checks that
//   the whole number fits, so to_i32 of -2147483648d9_f64 is -2147483648_i32
//   and to_u32 of -0d9_f32 is 0_u32, but to_u32 of -1_i32 is a runtime error.
//   Narrowing to f32 checks range before rounding, and an i64 or u64 past 2^53
//   rounds to the nearest f64. f64 uses double precision; printing uses 17
//   significant digits (f32 keeps 6).
// - Real math operations preserve f32 or f64. math_to_log is natural
//   logarithm, math_to_log10 is base ten, and math_to_exp is the natural
//   exponential. Logarithms require positive inputs; math_to_asin and
//   math_to_acos require inputs in [-1, 1]. All angles, including inverse-trig
//   results, are radians. math_to_tan uses the stored approximate angle; a
//   near-pole angle may give a large finite result.
//   Nonfinite inputs and results are rejected; underflow may round to zero.
//   eq_3 and eq_6 use absolute-or-relative tolerances 1e-3 and 1e-6 for both
//   real types. Real numbers do not use exact eq or not_eq.
// - math_to_log1p computes log(1+x) without first rounding 1+x, and requires
//   x > -1. math_to_expm1 computes exp(x)-1 without subtracting nearly equal
//   numbers. math_to_hypot takes two same-type reals and computes their
//   Euclidean norm without unnecessarily squaring huge or tiny operands. These
//   helpers preserve f32 or f64 and reject results outside that type's finite
//   range.
// - A top-level const NAME is value binds an immutable scalar literal, an
//   earlier constant, or a config value, visible to all later code, including
//   functions and array rows. Its name cannot be redeclared or shadowed. A
//   constant cannot depend on a runtime variable or a computed expression.
//   For example: const pi is 3d141592653589793_f64.
// - An INTEGER must fit in its type, with its minus sign, so -0_u32 is 0_u32,
//   but -1_u32 is a syntax error.
// - break and continue apply to the innermost LOOP or while. A LOOP counts
//   with an i32, and sleep_ms waits a u32 number of milliseconds.
// - A STRING has no escapes, and cannot hold a quote, a backslash, a newline or
//   NUL. A str can be stored, printed, compared with eq and not_eq, and set
//   with set ... to, but not computed with, as to_add and the other updates
//   are only of numbers. A bool is the same, and is printed as true or false.
// - new str of joins its values, as info prints them, into a str that it
//   allocates, of at most BOSONOGA_NEW_STR_LENGTH_MAX bytes. Its var or val
//   owns it, and unset frees it and ends the variable, whose name can then be
//   declared again. The unset is a statement of the block of its new, before
//   the end of that block, and before a return, or a break or continue of a
//   loop around the new. So that nothing points into it after its unset, it
//   cannot be set, and is only read by info, a comparison or new str of.
// - A program has either one bosonoga_main or the sections of a gfx program,
//   before either of which it can have one bosonoga_global, and any number of
//   functions and arrays. Each can only be used after its definition, so a
//   function can call itself but two cannot call each other.
// - A function sees constants, its parameters and its own variables. Everything
//   is passed and returned by value. The INDEXED types after ret, and those of
//   an array, are numbered from 0 in order. A return gives one value per type,
//   and the end of the block is a runtime error for a function with ret. A
//   function that returns nothing says ret_void, and its return has no
//   values.
// - A caller names each value that it takes with its index and type. var and
//   set ... to take a call that returns one value with "with ret 0_TYPE".
//   Otherwise "call ... with ret" and a block take them, in the same way for a
//   function and an array. Each taken value names one of them for the block,
//   as in 0_f64 as distance, so that it is a variable of the block, which can
//   take any of them, once each, by increasing index. Words like ret_0_i32
//   are reserved, and are not values:
//
//     call task at index with ret
//       0_f64 as distance
//       2_f64 as expected
//     di
//       ...
//     do
//
//   Calls nest at most BOSONOGA_CALL_DEPTH_MAX deep, and a function cannot have
//   the statements of gfx_frame.
// - The values of a call follow "of", and a call of a function without
//   parameters has none.
// - An array holds literal or constant rows. Read it with "at" and one i32
//   index from 0 to return that row. An index without a row is a runtime error.
// - bosonoga_global runs first and cannot return. The variables at the top of
//   its block are shared by bosonoga_main and all sections.
// - A gfx program has one gfx_main and at most one of each other section, in
//   any order. Only gfx_main can return. In a window, see gfx.h, gfx_init runs
//   once, gfx_frame once per frame, gfx_main across frames until it returns,
//   gfx_key_down once each time a key is pressed, but not as it repeats while
//   held, and gfx_cleanup when the window closes. Without a window, gfx_frame
//   and gfx_key_down never run.
// - A KEY is a key of the [color], [global] or [key] group of the settings,
//   see settings.h. A color can be stored and printed, and is printed as
//   RRGGBB. A global is one INTEGER or REAL. A key of [key] names a key of the
//   keyboard as miniquad does, so A to Z, Key0 to Key9, Space, Escape, Enter,
//   Tab, Backspace, Left, Right, Up or Down, and its value is an input_key. An
//   input_key can be stored, printed by that name, and compared with eq or
//   not_eq. gfx_key is the input_key pressed, and is only in gfx_key_down.
// - gfx_clear, gfx_draw_rectangle (x, y, width, height, color, with y growing
//   down), gfx_draw_hex and gfx_draw_circle (center x, center y, radius,
//   color) and gfx_delta_time (seconds since the previous frame) are only in
//   gfx_frame. A hex is regular and flat-top, with a corner straight right of
//   its center, and its radius is from the center to a corner, so that it is
//   2 radius wide and sqrt(3) radius high. A hex or circle whose radius is not
//   above zero is not drawn. Shapes are drawn in the order of their
//   statements, each over the ones before it.

typedef enum {
  BOSONOGA_SECTION_MAIN,
  BOSONOGA_SECTION_GFX_INIT,
  BOSONOGA_SECTION_GFX_FRAME,
  BOSONOGA_SECTION_GFX_CLEANUP,
  BOSONOGA_SECTION_GFX_KEY_DOWN,
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
  BOSONOGA_OP_CONVERT_TO_F64,
  BOSONOGA_OP_CONVERT_TO_I32,
  BOSONOGA_OP_CONVERT_TO_I64,
  BOSONOGA_OP_CONVERT_TO_U32,
  BOSONOGA_OP_CONVERT_TO_U64,
  BOSONOGA_OP_SIN,
  BOSONOGA_OP_COS,
  BOSONOGA_OP_ATAN2,
  BOSONOGA_OP_SQRT,
  BOSONOGA_OP_POW,
  BOSONOGA_OP_ABS,
  BOSONOGA_OP_MIN,
  BOSONOGA_OP_MAX,
  BOSONOGA_OP_FLOOR,
  BOSONOGA_OP_CEIL,
  BOSONOGA_OP_ROUND,
  BOSONOGA_OP_LOG,
  BOSONOGA_OP_LOG10,
  BOSONOGA_OP_EXP,
  BOSONOGA_OP_ASIN,
  BOSONOGA_OP_ACOS,
  BOSONOGA_OP_TAN,
  BOSONOGA_OP_LOG1P,
  BOSONOGA_OP_EXPM1,
  BOSONOGA_OP_HYPOT,
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
  BOSONOGA_OP_DRAW_HEX,
  BOSONOGA_OP_DRAW_CIRCLE,
  BOSONOGA_OP_GFX_DELTA_TIME,
  BOSONOGA_OP_GFX_KEY,
  BOSONOGA_OP_ARGUMENT,
  BOSONOGA_OP_CALL,
  BOSONOGA_OP_RESULT,
  BOSONOGA_OP_FUNCTION_RETURN,
  BOSONOGA_OP_TAKE_RESULT,
  BOSONOGA_OP_ARRAY_READ,
  BOSONOGA_OP_NEW_STR_PART,  // Appends a value to the str that new makes.
  BOSONOGA_OP_NEW_STR,
  BOSONOGA_OP_UNSET,
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
  BosonogaOperand left;
  BosonogaOperand right;
  BosonogaOperand third; // gfx_draw statement
  BosonogaOperand fourth; // gfx_draw statement
  BosonogaOperand fifth; // gfx_draw statement
} BosonogaOp;

// The most bytes of a str that new makes.
#define BOSONOGA_NEW_STR_LENGTH_MAX 1024

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
