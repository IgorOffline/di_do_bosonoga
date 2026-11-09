#ifndef BOSONOGA_PARSER_INTERNAL_H_
#define BOSONOGA_PARSER_INTERNAL_H_

#include <glib.h>

#include "bosonoga/bosonoga.h"
#include "bosonoga/parser.h"
#include "tokenizer.h"

// What the files of the parser share, which the rest of the program
// does not need. The public interface and the grammar are in parser.h.

#define BOSONOGA_SHIFT_PREFIX "shift_"
#define BOSONOGA_LOOP_PREFIX "to_"
#define BOSONOGA_COLOR_PREFIX "color_"
#define BOSONOGA_GLOBAL_PREFIX "global_"
#define BOSONOGA_DELTA_TIME_WORD "gfx_delta_time"

typedef struct {
  const gchar *name;  // Slice of the source.
  gsize length;
  BosonogaValueKind kind;
  guint slot;
} BosonogaVariable;

typedef struct {
  BosonogaTokenizer tokenizer;
  GArray *ops;
  GArray *variables;  // Of BosonogaVariable, the ones visible at the cursor.
  guint slot_count;
  guint block_depth;
  BosonogaSection section;  // The section that the cursor is in.
  guint delta_time_slot;    // Holds gfx_delta_time in gfx_frame.
  gboolean has_section[BOSONOGA_SECTION_COUNT];
  BosonogaProgram *program;
  const BosonogaSettings *settings;
} BosonogaParser;

// support.c: tokens, errors and ops.

gboolean bosonoga_token_is_word(const BosonogaToken *token, const gchar *word);

// Returns whether the token is a word made of the prefix
// and at least one digit.
gboolean bosonoga_token_is_numbered(const BosonogaToken *token,
                                    const gchar *prefix);

// Returns whether the token is a word for a value of the settings, the color
// prefix and a key of [color] or the global prefix and a key of [global], and
// stores the value of the key in value.
gboolean bosonoga_token_setting(const BosonogaParser *parser,
                                const BosonogaToken *token,
                                BosonogaValue *value);

gboolean bosonoga_token_is_reserved(const BosonogaParser *parser,
                                    const BosonogaToken *token);

// Reads the digits after the prefix of a word
// that bosonoga_token_is_numbered accepted.
gboolean bosonoga_numbered_value(const BosonogaToken *token,
                                 const gchar *prefix, guint64 max,
                                 guint64 *value);

const gchar *bosonoga_value_kind_name(BosonogaValueKind kind);

// Returns a new string with the start of the token, to show in a message. The
// source is arbitrary bytes and the cut can split a character, while GError
// messages must be UTF-8.
gchar *bosonoga_token_shown(const BosonogaToken *token);

gboolean bosonoga_fail_at(const BosonogaParser *parser,
                          const BosonogaToken *token, GError **error,
                          BosonogaError code, const gchar *format, ...)
    G_GNUC_PRINTF(5, 6);

gboolean bosonoga_parse_error(const BosonogaParser *parser,
                              const gchar *expected, GError **error);

gboolean bosonoga_advance(BosonogaParser *parser, GError **error);

gboolean bosonoga_at_word(const BosonogaParser *parser, const gchar *word);

gboolean bosonoga_expect_word(BosonogaParser *parser, const gchar *word,
                              GError **error);

// Appends a zeroed op located at the token. The pointer is valid until
// the next op is emitted.
BosonogaOp *bosonoga_emit(BosonogaParser *parser, BosonogaOpKind kind,
                          const BosonogaToken *token);

void bosonoga_patch_jumps_to_here(BosonogaParser *parser, const GArray *jumps);

// value.c: variables and values.

const BosonogaVariable *bosonoga_find_variable(const BosonogaParser *parser,
                                               const BosonogaToken *token);

void bosonoga_declare_variable(BosonogaParser *parser,
                               const BosonogaToken *name,
                               BosonogaValueKind kind, guint slot);

// Reads the name of a new variable, which the caller declares once its initial
// value is parsed, so that the value cannot refer to the variable.
gboolean bosonoga_parse_new_name(BosonogaParser *parser, BosonogaToken *name,
                                 GError **error);

gboolean bosonoga_at_value(const BosonogaParser *parser);

gboolean bosonoga_parse_value(BosonogaParser *parser, BosonogaOperand *operand,
                              GError **error);

// Parses a value that must have the kind that the context expects.
gboolean bosonoga_parse_value_of_kind(BosonogaParser *parser,
                                      BosonogaValueKind kind,
                                      const gchar *context,
                                      BosonogaOperand *operand, GError **error);

// assignment.c: var and set statements.

// Parses a var statement, whose keyword is already consumed.
gboolean bosonoga_parse_var(BosonogaParser *parser, GError **error);

// Parses a set statement, whose keyword is already consumed.
gboolean bosonoga_parse_set(BosonogaParser *parser, GError **error);

// condition.c: the conditions of if statements.

// Parses a condition, leaving the ops right after it as what runs
// if it is true. The caller patches false_jumps to what runs if it is false.
gboolean bosonoga_parse_condition(BosonogaParser *parser, GArray *false_jumps,
                                  GError **error);

// statement.c: blocks and the statements in them.

gboolean bosonoga_parse_block(BosonogaParser *parser, GError **error);

// control.c: if statements and loops.

// Parses an if statement, whose keyword is already consumed.
gboolean bosonoga_parse_if(BosonogaParser *parser, GError **error);

// Parses a loop, whose keyword is the current token.
gboolean bosonoga_parse_loop(BosonogaParser *parser, GError **error);

#endif  // BOSONOGA_PARSER_INTERNAL_H_
