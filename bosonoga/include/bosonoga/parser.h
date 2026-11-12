#ifndef BOSONOGA_PARSER_H_
#define BOSONOGA_PARSER_H_

#include <bosonoga/settings.h>
#include <glib.h>

// The grammar of a .bosonoga program. Whitespace separates tokens, and //
// starts a comment that runs to the end of the line.
//
//   program    := item+
//   item       := ("bosonoga_global" | "bosonoga_main") block | section
//                 | function | "val" NAME "is" array
//                 | "const" NAME "is" (value | len)
//   section    := ("gfx_init" | "gfx_frame" | "gfx_cleanup" | "gfx_key_down"
//                 | "gfx_main") block
//   function   := "def" FUNCTION PARAMETER* ("ret" INDEXED+ | "ret_void")
//                 block
//   array      := "array" INDEXED+ "di" row+ "do"
//   row        := "row" value+
//   block      := "di" statement+ "do"
//   statement  := "info" value+
//               | ("var" | "val") NAME "is"
//                 (initial | call "with" "ret" INDEXED | new)
//               | "var" NAME "is" (array | "array" INDEXED+ "rows" value)
//               | "unset" NAME
//               | "set" NAME update
//               | "set" NAME "to" (value | len | call "with" "ret" INDEXED)
//               | "set" NAME "at" value "to" value+
//               | "call" call ("with" "ret" taken+ block)?
//               | "if" condition block ("else" "if" condition block)*
//                 ("else" block)?
//               | LOOP ("with" NAME)? block
//               | "while" condition block
//               | "break" | "continue" | "nihil"
//               | "sleep_ms" value
//               | "gfx_await_next_frame"
//               | "return" ("exit_success" | "exit_failure" | value*)
//               | "gfx_clear" value
//               | "gfx_draw_rectangle" value value value value value
//               | "gfx_draw_hex" value value value value
//               | "gfx_draw_circle" value value value value
//               | "gfx_draw_text" HALIGN VALIGN value value value value value
//                 value+
//               | "gfx_draw_icon" value value value value value
//   initial    := conversion | len | value ("shift_or" value)*
//   len        := ("len_col" | "len_row") NAME
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
//               | KEY_VALUE | FONT | ICON | "gfx_delta_time" | "gfx_key"
//               | NAME
//   HALIGN     := "left" | "center" | "right"
//   VALIGN     := "top" | "middle" | "baseline" | "bottom"
//   TYPE       := "i32" | "i64" | "u32" | "u64" | "f32" | "f64" | "flags"
//               | "color" | "str" | "bool" | "input_key" | "font" | "icon"
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
//   FONT       := "font_" KEY
//   ICON       := "icon_" KEY
//   LOOP       := "to_" digit+
//
// - "and" binds tighter than "or", and "not" negates only the comparison after
//   it. A comparison without compare is of a bool, so "if done" is "if done eq
//   true_bool". A variable is visible to the end of its block and cannot hide
//   another.
// - A val is never set after its declaration, but unlike a const it can hold a
//   runtime value, as in val half is to_f64 count.
// - to_subtract and to_divide apply the other values to the first, and a single
//   value is negated or inverted. to_mod keeps the remainder with the sign of
//   the dividend, as % and fmod in C, and needs at least two values. Integer
//   division truncates toward zero. The values in between are exact in a signed
//   128-bit integer, and only the stored result has to fit its type, so to_add
//   9223372036854775807_i64 1_i64 -1_i64 is the largest i64. Dividing by zero,
//   an input outside the domain of a math function and a result out of range
//   are runtime errors.
// - math_to_abs keeps the type, and of the smallest i32 or i64 is a runtime
//   error. math_to_min and math_to_max take two numbers of one type. The real
//   math functions keep f32 or f64, take and give radians, and reject nonfinite
//   inputs and results. math_to_atan2 takes y, then x, and math_to_round rounds
//   halves away from zero. math_to_log1p, math_to_expm1 and math_to_hypot avoid
//   the rounding of 1+x and exp(x)-1, and the overflow of squaring.
// - Conversions are explicit, as arithmetic, comparisons and binary math
//   functions need matching types. A conversion to an integer truncates toward
//   zero and then checks that it fits, so to_u32 of -0d9_f32 is 0_u32, but
//   to_u32 of -1_i32 is a runtime error. f64 prints 17 significant digits and
//   f32 6. eq_3 and eq_6 compare reals within an absolute-or-relative 1e-3 and
//   1e-6, and reals have no exact eq or not_eq.
// - A top-level const binds a literal, an earlier constant or a setting, is
//   visible to all later code, including functions and array rows, and cannot
//   be redeclared or shadowed.
// - An INTEGER must fit in its type, with its minus sign, so -0_u32 is 0_u32,
//   but -1_u32 is a syntax error.
// - break and continue apply to the innermost LOOP or while. A LOOP counts
//   with an i32, and sleep_ms waits a u32 number of milliseconds.
// - A STRING has no escapes, and cannot hold a quote, a backslash, a newline or
//   NUL. A str or a bool can be stored, printed, compared with eq and not_eq,
//   and set with set ... to, but not computed with. A bool prints as true or
//   false.
// - new str of joins its values, as info prints them, into a str of at most
//   BOSONOGA_NEW_STR_LENGTH_MAX bytes, which its var or val owns. unset frees
//   it and the name, and is in the block of the new, before its end and before
//   a return, break or continue that leaves it. So that nothing points into it
//   after that, it cannot be set, and is only read by info, a comparison or
//   new str of.
// - A program has either one bosonoga_main or the sections of a gfx program,
//   after at most one bosonoga_global, and any number of functions and arrays.
//   Each can only be used after its definition, so a function can call itself
//   but two cannot call each other.
// - A function sees constants, its parameters and its own variables, and
//   everything is passed and returned by value. The INDEXED types after ret,
//   and those of an array, are numbered from 0. A return gives one value per
//   type, and the end of the block of a function with ret is a runtime error.
//   A function that returns nothing says ret_void.
// - A caller names each value that it takes by its index and type. var and
//   set ... to take a call of one value with "with ret 0_TYPE". Otherwise
//   "call ... with ret" names values for its block, once each, by increasing
//   index, in the same way for a function and an array. Words like ret_0_i32
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
// - An array has rows of the INDEXED types, and "at" an i32 index from 0 reads
//   one. An index without a row is a runtime error. A val array is top-level,
//   with literal or constant rows, and is seen by all later code. A var array
//   is a variable of a block, with such rows, or "rows" and an i32 literal or
//   constant of zeroed ones (0, false, an empty str or 000000, but no input_key
//   or font), made again each time its declaration runs. It holds at most
//   BOSONOGA_ARRAY_VALUES_MAX values, and set ... at writes a whole row, but it
//   is not set whole, passed, printed or compared. len_col is the length of a
//   column, the number of rows, and len_row the length of a row, an i32 known
//   before the program runs.
// - bosonoga_global runs first and cannot return. The variables at the top of
//   its block are shared by bosonoga_main and all sections.
// - A gfx program has one gfx_main and at most one of each other section, in
//   any order. Only gfx_main can return. In a window, see gfx.h, gfx_init runs
//   once, gfx_frame once per frame, gfx_main across frames until it returns,
//   gfx_key_down once per press, not as the key repeats, and gfx_cleanup when
//   the window closes. Without a window, gfx_frame and gfx_key_down never run.
// - gfx_await_next_frame waits for the next frame, so that a loop in gfx_main
//   steps once per frame, at most 60 times a second, and does not use up the
//   ops of a frame. It is only in gfx_main, and not in a function.
// - A KEY is a key of the [color], [global], [key], [font] or [icon] group of
//   the settings, see settings.h. A color prints as RRGGBB, and a global is one
//   INTEGER or REAL. A key of [key] names a keyboard key by its sokol_app name,
//   like SAPP_KEYCODE_ESCAPE, and is an input_key, which prints by that name.
//   A key of [font] names a TrueType file and a key of [icon] a PNG file,
//   relative to the settings, and is a font or an icon, which prints as the
//   settings have it. Only a window reads the files, before gfx_init. An
//   input_key, font or icon compares only with eq or not_eq.
//   gfx_key is the input_key pressed, and is only in gfx_key_down.
// - gfx_clear, the gfx_draw statements and gfx_delta_time (seconds since the
//   previous frame) are only in gfx_frame. Shapes, with y growing down, are
//   drawn in the order of their statements, each over the ones before it. A
//   rectangle is x, y, width, height and color, and a hex or circle center x,
//   center y, radius and color. A hex is regular and flat-top, with a corner
//   straight right of its center, which its radius reaches. A hex, circle or
//   text whose radius or size is not above zero is not drawn.
// - gfx_draw_text (align, x, y, font, size, color, then what it draws) joins
//   its values as info prints them, into at most BOSONOGA_DRAW_TEXT_LENGTH_MAX
//   bytes, and draws them in one line with fontstash. HALIGN and VALIGN say
//   where x and y are on the text, from the ascender and descender of the font,
//   and are not reserved words. Size is the f32 pixel height from the ascender
//   to the descender, so 20 pixels per em of Intel One Mono is 27d6_f32.
// - gfx_draw_icon (center x, center y, icon, angle, color) draws the alpha of
//   the PNG in color, at its own size of at most BOSONOGA_ICON_SIDE_MAX pixels
//   a side, turned clockwise by the f32 angle in radians. Unturned, its corner
//   is on a whole pixel, so that it is sharp.

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

// The values of fontstash, one horizontal and one vertical for a text.
typedef enum {
  BOSONOGA_GFX_ALIGN_LEFT = 1 << 0,
  BOSONOGA_GFX_ALIGN_CENTER = 1 << 1,
  BOSONOGA_GFX_ALIGN_RIGHT = 1 << 2,
  BOSONOGA_GFX_ALIGN_TOP = 1 << 3,
  BOSONOGA_GFX_ALIGN_MIDDLE = 1 << 4,
  BOSONOGA_GFX_ALIGN_BOTTOM = 1 << 5,
  BOSONOGA_GFX_ALIGN_BASELINE = 1 << 6,
} BosonogaGfxAlign;

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
  BOSONOGA_OP_AWAIT_NEXT_FRAME,
  BOSONOGA_OP_RETURN,
  BOSONOGA_OP_MISSING_RETURN,
  BOSONOGA_OP_SECTION_END,
  BOSONOGA_OP_GFX_CLEAR,
  BOSONOGA_OP_DRAW_RECTANGLE,
  BOSONOGA_OP_DRAW_HEX,
  BOSONOGA_OP_DRAW_CIRCLE,
  BOSONOGA_OP_DRAW_TEXT_PART,
  BOSONOGA_OP_DRAW_TEXT,
  BOSONOGA_OP_DRAW_ICON,
  BOSONOGA_OP_GFX_DELTA_TIME,
  BOSONOGA_OP_GFX_KEY,
  BOSONOGA_OP_ARGUMENT,
  BOSONOGA_OP_CALL,
  BOSONOGA_OP_RESULT,
  BOSONOGA_OP_FUNCTION_RETURN,
  BOSONOGA_OP_TAKE_RESULT,
  BOSONOGA_OP_ARRAY_READ,
  BOSONOGA_OP_ARRAY_INIT,
  BOSONOGA_OP_ARRAY_WRITE,
  BOSONOGA_OP_NEW_STR_PART,
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
  gint32 integer;  // Also the BosonogaGfxAlign of gfx_draw_text.
  BosonogaOperand left;
  BosonogaOperand right;
  // Of the gfx_draw statements.
  BosonogaOperand third;
  BosonogaOperand fourth;
  BosonogaOperand fifth;
} BosonogaOp;

#define BOSONOGA_DRAW_TEXT_LENGTH_MAX 1024

// The smallest most that OpenGL 3.3 guarantees for a texture.
#define BOSONOGA_ICON_SIDE_MAX 1024

// A font or icon that the program names, which a window loads before gfx_init.
typedef struct {
  gchar *key;   // Of [font] or [icon].
  gchar *text;  // As the settings have it, printed for its value.
  gchar *path;
} BosonogaProgramAsset;

#define BOSONOGA_NEW_STR_LENGTH_MAX 1024

// The most values that a function takes or returns, and of a row.
#define BOSONOGA_FUNCTION_VALUES_MAX 16
#define BOSONOGA_CALL_DEPTH_MAX 10000
#define BOSONOGA_ARRAY_VALUES_MAX 65536

// An array is read like a function of its index that returns a row.
typedef struct {
  const gchar *name;  // Slice of the source.
  gsize name_length;
  gboolean is_array;
  gboolean is_var;     // Of an array that is a variable, in slots from slot.
  gboolean is_zeroed;  // Then its rows are copies of the row at first_value.
  guint slot;
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
  // Of BosonogaProgramAsset, numbered by a font or icon value.
  GArray *fonts;
  GArray *icons;
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
