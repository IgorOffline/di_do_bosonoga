#ifndef BOSONOGA_PARSER_H_
#define BOSONOGA_PARSER_H_

#include <bosonoga/settings.h>
#include <glib.h>

// The grammar of a .bosonoga program. Whitespace separates tokens, and //
// starts a comment that runs to the end of the line.
//
//   program    := item+
//   item       := ("bosonoga_global" | "bosonoga_main") block | section
//                 | "pub"? (function | "val" NAME "is" array
//                   | "const" NAME "is" (value | len))
//                 | ("io_raw_import" | "mod") STRING
//   section    := ("gfx_init" | "gfx_frame" | "gfx_cleanup" | "gfx_key_down"
//                 | "gfx_main") block
//   function   := "def" FUNCTION PARAMETER* ("ret" INDEXED+ | "ret_void")
//                 block
//   array      := "array" INDEXED+ "di" row+ "do"
//   row        := "row" value+
//   block      := "di" statement+ "do"
//   statement  := "info" value+
//               | "pub"? ("var" | "val") NAME "is"
//                 (initial | call "with" "ret" INDEXED | new)
//               | "pub"? "var" NAME "is"
//                 (array | "array" INDEXED+ "with_len_col" value)
//               | "unset" NAME
//               | "set" NAME update
//               | "set" NAME "to" (value | len | call "with" "ret" INDEXED)
//               | "set" NAME "at" value "to" value+
//               | "set" NAME "to_join" value+
//               | "call" call ("with" "ret" taken+ block)?
//               | given "with" "ret" taken+ block
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
//               | "new" "xml_str" ("of" value+ | "element" STRING
//                 (STRING value)* ("of" value+)?)
//   given      := "io_read" value value | "io_write" value value value
//               | "io_modified" value value | "xml_child" value STRING value
//               | "xml_attribute" value STRING | "str_parse" value
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
//               | KEY_VALUE | FONT | ICON | DIR | "gfx_delta_time"
//               | "gfx_key" | NAME
//   HALIGN     := "left" | "center" | "right"
//   VALIGN     := "top" | "middle" | "baseline" | "bottom"
//   TYPE       := "i32" | "i64" | "u32" | "u64" | "f32" | "f64" | "flags"
//               | "color" | "str" | "bool" | "input_key" | "font" | "icon"
//               | "xml_str" | "dir"
//   FUNCTION   := NAME "_fn", like divide_fn, which no other name is
//   PARAMETER  := NAME, which ends with "_" TYPE, like count_i32
//   INDEXED    := digit+ "_" TYPE, like 0_i32 and 1_str
//   INTEGER    := "-"? digit+ ("_i32" | "_i64" | "_u32" | "_u64")
//   REAL       := "-"? digit+ ("d" digit+)? ("_f32" | "_f64")
//   STRING     := '"' character* '"' ("_str" | "_xml_str")
//   BOOL       := "true_bool" | "false_bool"
//   SHIFT      := "shift_" digit+
//   COLOR      := "color_" KEY
//   GLOBAL     := "global_" KEY
//   KEY_VALUE  := "key_" KEY
//   FONT       := "font_" KEY
//   ICON       := "icon_" KEY
//   DIR        := "dir_" KEY
//   LOOP       := "to_" digit+
//
// - "and" binds tighter than "or", and "not" negates only the comparison after
//   it. "if done" is "if done eq true_bool". A variable is visible to the end
//   of its block and cannot hide another.
// - A val is never set after its declaration, and a top-level const binds a
//   literal, an earlier constant or a setting.
// - to_subtract and to_divide apply the other values to the first, and a single
//   value is negated or inverted. The values in between are exact in 128 bits,
//   and only the stored result has to fit its type. Overflow, dividing by zero
//   and an input outside the domain of a math function are runtime errors.
// - Conversions are explicit, as arithmetic and comparisons need matching
//   types. Reals have no exact eq, and eq_3 and eq_6 compare within 1e-3 and
//   1e-6.
// - A STRING has no escapes. new str of joins its values, as info prints them,
//   into a str that its var or val owns, and unset frees it in its block. An
//   xml_str is a str that is well-formed XML, see xml.h.
// - A given statement gives its block 0_bool, whether it could, and then its
//   values, as call ... with ret does.
// - A program has either one bosonoga_main or the sections of a gfx program,
//   after at most one bosonoga_global, whose top variables they share. Each
//   item is used after its definition, so two functions cannot call each
//   other, and what is declared must be used.
// - A function sees constants, its parameters and its own variables, and
//   everything is passed by value. A caller names each value it takes by its
//   index and type:
//
//     call task at index with ret
//       0_f64 as distance
//       2_f64 as expected
//     di
//       ...
//     do
//
// - An array is read like a function of an i32 index from 0 that returns a
//   row. A val array is top-level, with literal rows, and a var array has
//   literal rows or with_len_col zeroed ones, made again each time its
//   declaration runs. set ... at writes a whole row. len_col is the number of
//   rows and len_row the length of one.
// - io_raw_import reads the tokens of a file next to the program in its place.
//   mod reads a module, whose names are private to it unless pub, which shows
//   them to the file that read it. Each file is read at most once, and a name
//   is unique in the whole program.
// - In a window, see gfx.h, gfx_init runs once, gfx_frame once per frame,
//   gfx_main across frames until it returns, gfx_key_down once per press and
//   gfx_cleanup when it closes. gfx_await_next_frame in gfx_main waits for the
//   next frame. Drawing and gfx_delta_time are only in gfx_frame, and gfx_key
//   only in gfx_key_down.
// - A KEY names an entry of the settings, see settings.h. Shapes, with y
//   growing down, are drawn in order.

typedef enum {
  BOSONOGA_SECTION_MAIN,
  BOSONOGA_SECTION_GFX_INIT,
  BOSONOGA_SECTION_GFX_FRAME,
  BOSONOGA_SECTION_GFX_CLEANUP,
  BOSONOGA_SECTION_GFX_KEY_DOWN,
  BOSONOGA_SECTION_GLOBAL,
  BOSONOGA_SECTION_COUNT,
} BosonogaSection;

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
  BOSONOGA_OP_XML_PART,    // Joins a BosonogaXmlPart, which integer is.
  BOSONOGA_OP_JOIN_STORE,  // The joined xml_str replaces the one in slot.
  BOSONOGA_OP_IO_READ,     // With integer the results taken, as of the rest.
  BOSONOGA_OP_IO_WRITE,
  BOSONOGA_OP_IO_MODIFIED,
  BOSONOGA_OP_XML_CHILD,
  BOSONOGA_OP_XML_ATTRIBUTE,
  BOSONOGA_OP_STR_PARSE,
} BosonogaOpKind;

typedef enum {
  BOSONOGA_XML_PART_RAW,
  BOSONOGA_XML_PART_TEXT,  // A value as info prints it, escaped.
  BOSONOGA_XML_PART_OPEN,
  BOSONOGA_XML_PART_NAME,
  BOSONOGA_XML_PART_VALUE,
  BOSONOGA_XML_PART_START,
  BOSONOGA_XML_PART_EMPTY,
  BOSONOGA_XML_PART_CLOSE,
} BosonogaXmlPart;

typedef struct {
  BosonogaOpKind kind;
  BosonogaCompareKind compare;
  gboolean negate;
  const gchar *path;
  gsize line;
  gsize column;
  guint slot;
  guint target;
  guint function;
  guint index;
  gint32 integer;  // Also the BosonogaGfxAlign of gfx_draw_text.
  BosonogaValueKind made;
  BosonogaOperand left;
  BosonogaOperand right;
  BosonogaOperand third;
  BosonogaOperand fourth;
  BosonogaOperand fifth;
} BosonogaOp;

#define BOSONOGA_DRAW_TEXT_LENGTH_MAX 1024

// The smallest most that OpenGL 3.3 guarantees for a texture.
#define BOSONOGA_ICON_SIDE_MAX 1024

typedef struct {
  gchar *key;
  gchar *text;  // As the settings have it, printed for its value.
  gchar *path;
} BosonogaProgramAsset;

#define BOSONOGA_NEW_STR_LENGTH_MAX 1024

#define BOSONOGA_XML_STR_LENGTH_MAX 1048576

#define BOSONOGA_FUNCTION_VALUES_MAX 16
#define BOSONOGA_CALL_DEPTH_MAX 10000
#define BOSONOGA_ARRAY_VALUES_MAX 65536

typedef struct {
  const gchar *name;
  gsize name_length;
  gboolean is_array;
  gboolean is_var;     // In slots from slot.
  gboolean is_zeroed;  // Then its rows are copies of the row at first_value.
  guint slot;
  guint entry;
  guint parameter_count;
  guint result_count;
  BosonogaValueKind parameters[BOSONOGA_FUNCTION_VALUES_MAX];
  BosonogaValueKind results[BOSONOGA_FUNCTION_VALUES_MAX];
  guint slot_count;  // Of its own frame, parameters first.
  guint row_count;
  guint first_value;  // Index in program->rows of row 0.
} BosonogaFunction;

typedef struct {
  gchar *path;  // UTF-8, as error messages show it.
  gchar *source;
} BosonogaProgramImport;

typedef struct {
  const gchar *path;  // Not owned.
  GArray *imports;
  GArray *ops;
  guint slot_count;  // Not of functions.
  GArray *functions;
  GArray *rows;  // Of all arrays, one row after another.
  GArray *fonts;
  GArray *icons;
  GArray *dirs;
  gboolean is_gfx;
  // A missing section starts at a BOSONOGA_OP_SECTION_END, so it can still run.
  guint entries[BOSONOGA_SECTION_COUNT];
} BosonogaProgram;

// The program points into path and source, which must outlive it. Path is
// UTF-8, and io_raw_import reads the files next to it.
BosonogaProgram *bosonoga_parse(const gchar *path, const gchar *source,
                                gsize length, const BosonogaSettings *settings,
                                GError **error);

void bosonoga_program_free(BosonogaProgram *program);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(BosonogaProgram, bosonoga_program_free)

#endif  // BOSONOGA_PARSER_H_
