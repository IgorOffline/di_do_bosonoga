#ifndef BOSONOGA_PARSER_INTERNAL_H_
#define BOSONOGA_PARSER_INTERNAL_H_

#include <glib.h>

#include "bosonoga/bosonoga.h"
#include "bosonoga/parser.h"
#include "tokenizer.h"

// Shared by the files of the parser. Statement parsers are called after their
// keyword is consumed, except for those that read it from the current token.

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

// Indices in ops of jumps, patched at the end of the loop.
typedef struct {
  GArray *break_jumps;
  GArray *continue_jumps;
} BosonogaLoop;

typedef struct {
  BosonogaTokenizer tokenizer;
  GArray *ops;
  GArray *variables;
  guint slot_count;
  guint block_depth;
  GArray *loops;  // Of BosonogaLoop, innermost last.
  BosonogaSection section;
  guint delta_time_slot;
  gboolean has_section[BOSONOGA_SECTION_COUNT];
  BosonogaProgram *program;
  const BosonogaSettings *settings;
} BosonogaParser;

gboolean bosonoga_token_is_word(const BosonogaToken *token, const gchar *word);

gboolean bosonoga_token_is_numbered(const BosonogaToken *token,
                                    const gchar *prefix);

gboolean bosonoga_token_setting(const BosonogaParser *parser,
                                const BosonogaToken *token,
                                BosonogaValue *value);

gboolean bosonoga_token_is_reserved(const BosonogaParser *parser,
                                    const BosonogaToken *token);

gboolean bosonoga_numbered_value(const BosonogaToken *token,
                                 const gchar *prefix, guint64 max,
                                 guint64 *value);

const gchar *bosonoga_value_kind_name(BosonogaValueKind kind);

// The source is arbitrary bytes, but GError messages must be UTF-8.
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

// The op is zeroed, and the pointer is valid until the next emit.
BosonogaOp *bosonoga_emit(BosonogaParser *parser, BosonogaOpKind kind,
                          const BosonogaToken *token);

void bosonoga_patch_jumps(BosonogaParser *parser, const GArray *jumps,
                          guint target);

void bosonoga_patch_jumps_to_here(BosonogaParser *parser, const GArray *jumps);

const BosonogaVariable *bosonoga_find_variable(const BosonogaParser *parser,
                                               const BosonogaToken *token);

void bosonoga_declare_variable(BosonogaParser *parser,
                               const BosonogaToken *name,
                               BosonogaValueKind kind, guint slot);

// The caller declares the variable after its initial value, which so cannot
// refer to it.
gboolean bosonoga_parse_new_name(BosonogaParser *parser, BosonogaToken *name,
                                 GError **error);

gboolean bosonoga_at_value(const BosonogaParser *parser);

gboolean bosonoga_parse_value(BosonogaParser *parser, BosonogaOperand *operand,
                              GError **error);

gboolean bosonoga_parse_value_of_kind(BosonogaParser *parser,
                                      BosonogaValueKind kind,
                                      const gchar *context,
                                      BosonogaOperand *operand, GError **error);

gboolean bosonoga_parse_var(BosonogaParser *parser, GError **error);

gboolean bosonoga_parse_set(BosonogaParser *parser, GError **error);

// The ops after the condition run if it is true, and the caller patches
// false_jumps to where to go if it is false.
gboolean bosonoga_parse_condition(BosonogaParser *parser, GArray *false_jumps,
                                  GError **error);

gboolean bosonoga_parse_block(BosonogaParser *parser, GError **error);

gboolean bosonoga_parse_if(BosonogaParser *parser, GError **error);

gboolean bosonoga_parse_loop(BosonogaParser *parser, GError **error);

gboolean bosonoga_parse_while(BosonogaParser *parser,
                              const BosonogaToken *start, GError **error);

gboolean bosonoga_parse_loop_jump(BosonogaParser *parser,
                                  const BosonogaToken *start, GError **error);

void bosonoga_loop_clear(gpointer loop);

#endif  // BOSONOGA_PARSER_INTERNAL_H_
