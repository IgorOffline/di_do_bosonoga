#ifndef DARIA_PARSER_INTERNAL_H_
#define DARIA_PARSER_INTERNAL_H_

#include <glib.h>

#include "daria.h"
#include "parser/parser.h"
#include "tokenizer.h"

// What the files of the parser share, which the rest of the program
// does not need. The public interface and the grammar are in parser.h.

#define DARIA_SHIFT_PREFIX "shift_"
#define DARIA_LOOP_PREFIX "to_"

typedef struct {
  const gchar *name;  // Slice of the source.
  gsize length;
  DariaValueKind kind;
  guint slot;
} DariaVariable;

typedef struct {
  DariaTokenizer tokenizer;
  GArray *ops;
  GArray *variables;  // Of DariaVariable, the ones visible at the cursor.
  guint slot_count;
  guint block_depth;
  DariaSection section;  // The section that the cursor is in.
  gboolean has_section[DARIA_SECTION_COUNT];
  DariaProgram *program;
} DariaParser;

// support.c: tokens, errors and ops.

gboolean daria_token_is_word(const DariaToken *token, const gchar *word);

// Returns whether the token is a word made of the prefix
// and at least one digit.
gboolean daria_token_is_numbered(const DariaToken *token, const gchar *prefix);

// Returns whether the token is the word of a color, and stores which in color.
gboolean daria_token_color(const DariaToken *token, DariaColorName *color);

gboolean daria_token_is_reserved(const DariaToken *token);

// Reads the digits after the prefix of a word
// that daria_token_is_numbered accepted.
gboolean daria_numbered_value(const DariaToken *token, const gchar *prefix,
                              guint64 max, guint64 *value);

const gchar *daria_value_kind_name(DariaValueKind kind);

// Returns a new string with the start of the token, to show in a message. The
// source is arbitrary bytes and the cut can split a character, while GError
// messages must be UTF-8.
gchar *daria_token_shown(const DariaToken *token);

gboolean daria_fail_at(const DariaParser *parser, const DariaToken *token,
                       GError **error, DariaError code, const gchar *format,
                       ...) G_GNUC_PRINTF(5, 6);

gboolean daria_parse_error(const DariaParser *parser, const gchar *expected,
                           GError **error);

gboolean daria_advance(DariaParser *parser, GError **error);

gboolean daria_at_word(const DariaParser *parser, const gchar *word);

gboolean daria_expect_word(DariaParser *parser, const gchar *word,
                           GError **error);

// Appends a zeroed op located at the token. The pointer is valid until
// the next op is emitted.
DariaOp *daria_emit(DariaParser *parser, DariaOpKind kind,
                    const DariaToken *token);

void daria_patch_jumps_to_here(DariaParser *parser, const GArray *jumps);

// value.c: variables and values.

const DariaVariable *daria_find_variable(const DariaParser *parser,
                                         const DariaToken *token);

void daria_declare_variable(DariaParser *parser, const DariaToken *name,
                            DariaValueKind kind, guint slot);

// Reads the name of a new variable, which the caller declares once its initial
// value is parsed, so that the value cannot refer to the variable.
gboolean daria_parse_new_name(DariaParser *parser, DariaToken *name,
                              GError **error);

gboolean daria_at_value(const DariaParser *parser);

gboolean daria_parse_value(DariaParser *parser, DariaOperand *operand,
                           GError **error);

// Parses a value that must have the kind that the context expects.
gboolean daria_parse_value_of_kind(DariaParser *parser, DariaValueKind kind,
                                   const gchar *context, DariaOperand *operand,
                                   GError **error);

// assignment.c: var and set statements.

// Parses a var statement, whose keyword is already consumed.
gboolean daria_parse_var(DariaParser *parser, GError **error);

// Parses a set statement, whose keyword is already consumed.
gboolean daria_parse_set(DariaParser *parser, GError **error);

// condition.c: the conditions of if statements.

// Parses a condition, leaving the ops right after it as what runs
// if it is true. The caller patches false_jumps to what runs if it is false.
gboolean daria_parse_condition(DariaParser *parser, GArray *false_jumps,
                               GError **error);

// statement.c: blocks and the statements in them.

gboolean daria_parse_block(DariaParser *parser, GError **error);

// control.c: if statements and loops.

// Parses an if statement, whose keyword is already consumed.
gboolean daria_parse_if(DariaParser *parser, GError **error);

// Parses a loop, whose keyword is the current token.
gboolean daria_parse_loop(DariaParser *parser, GError **error);

#endif  // DARIA_PARSER_INTERNAL_H_
