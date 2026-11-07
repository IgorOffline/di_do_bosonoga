#ifndef DARIA_PARSER_H_
#define DARIA_PARSER_H_

#include <glib.h>

// The grammar of a .bosonoga program, in which whitespace only separates tokens
// and // starts a comment that runs to the end of the line:
//
//   program    := "main" block
//   block      := "di" statement+ "do"
//   statement  := "info" argument+
//               | "var" NAME "is" initial
//               | "set" NAME update
//               | "if" condition block ("else" "if" condition block)*
//                 ("else" block)?
//               | LOOP ("with" NAME)? block
//               | "nihil"
//               | "return" ("exit_success" | "exit_failure")
//   argument   := value | STRING
//   initial    := conversion | value ("shift_or" value)*
//   update     := conversion | ("to_add" | "to_subtract") value+
//   conversion := ("to_f32" | "to_i32") value
//   condition  := comparison (("and" | "or") comparison)*
//   comparison := value compare value
//   compare    := "gt" | "lt" | "eq" | "not_eq" | "eq_3" | "eq_6" | "not_eq_3"
//               | "not_eq_6" | "shift_has"
//   value      := INTEGER | REAL | SHIFT | NAME
//   INTEGER    := "-"? digit+ "_i32"
//   REAL       := "-"? digit+ ("d" digit+)? "_f32"
//   SHIFT      := "shift_" digit+
//   LOOP       := "to_" digit+
//
// "and" binds tighter than "or". A variable is visible until the end of its
// block and cannot hide another visible variable. to_subtract subtracts the
// other values from the first, and negates a single value.

typedef enum {
  DARIA_VALUE_I32,
  DARIA_VALUE_F32,
  DARIA_VALUE_FLAGS,
} DariaValueKind;

typedef struct {
  DariaValueKind kind;
  union {
    gint32 i32;
    gfloat f32;
    guint32 flags;
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
  DARIA_OP_ACCUMULATOR_STORE,     // slot = accumulator
  DARIA_OP_JUMP,                  // Continues at target.
  DARIA_OP_JUMP_UNLESS,           // Continues at target if compare is false.
  DARIA_OP_LOOP_TEST,             // Continues at target if slot >= integer.
  DARIA_OP_LOOP_NEXT,             // ++slot, and continues at target.
  DARIA_OP_RETURN,                // Ends the program with status integer.
  DARIA_OP_MISSING_RETURN,        // Fails: the program ended without a return.
} DariaOpKind;

typedef struct {
  DariaOpKind kind;
  DariaCompareKind compare;
  gsize line;
  gsize column;
  guint slot;
  guint target;  // Index of an op.
  gint32 integer;
  DariaOperand left;
  DariaOperand right;
  const gchar *text;  // Slice of the source, without the quotes.
  gsize length;
} DariaOp;

typedef struct {
  const gchar *path;  // Not owned.
  GArray *ops;        // Of DariaOp, always ends in DARIA_OP_MISSING_RETURN.
  guint slot_count;   // Variables and loop counters.
} DariaProgram;

// Returns a new program, or NULL on failure. The program points into path and
// source, which must outlive it. Path is only used in error messages, so it
// must be UTF-8.
DariaProgram *daria_parse(const gchar *path, const gchar *source, gsize length,
                          GError **error);

void daria_program_free(DariaProgram *program);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(DariaProgram, daria_program_free)

#endif  // DARIA_PARSER_H_
