#ifndef BOSONOGA_PARSER_INTERNAL_H_
#define BOSONOGA_PARSER_INTERNAL_H_

#include <glib.h>

#include "bosonoga/bosonoga.h"
#include "bosonoga/parser.h"
#include "tokenizer.h"
#include "value.h"

// Shared by the files of the parser. Statement parsers are called after their
// keyword is consumed, except for those that read it from the current token.

#define BOSONOGA_SHIFT_PREFIX "shift_"
#define BOSONOGA_LOOP_PREFIX "to_"
#define BOSONOGA_COLOR_PREFIX "color_"
#define BOSONOGA_GLOBAL_PREFIX "global_"
#define BOSONOGA_KEY_PREFIX "key_"
#define BOSONOGA_FONT_PREFIX "font_"
#define BOSONOGA_ICON_PREFIX "icon_"
#define BOSONOGA_DIR_PREFIX "dir_"
#define BOSONOGA_DELTA_TIME_WORD "gfx_delta_time"
#define BOSONOGA_KEY_WORD "gfx_key"
#define BOSONOGA_RESULT_PREFIX "ret_"
#define BOSONOGA_FUNCTION_SUFFIX "_fn"
#define BOSONOGA_VOID_WORD "ret_void"

// Of a name that the program declares, for who sees it and whether it is used.
typedef struct {
  BosonogaToken token;  // The name where it is declared.
  guint scope;          // In scopes.
  gboolean is_pub;
  gboolean is_used;
} BosonogaDeclaration;

// The program is scope 0, and each file read with mod has its own, inside the
// scope that read it.
typedef struct {
  const gchar *path;  // Of its file, as error messages show it.
  guint parent;       // Not of scope 0.
} BosonogaScope;

// Of the files that read the one being read, the innermost last.
typedef struct {
  BosonogaTokenizer tokenizer;  // After the name of the file it reads.
  gboolean is_module;           // Read with mod, rather than io_raw_import.
  guint scope;                  // Of the code around the file it reads.
} BosonogaOuterFile;

typedef struct {
  const gchar *name;
  gsize length;
  BosonogaValue value;
  BosonogaDeclaration declaration;
} BosonogaConstant;

typedef struct {
  const gchar *name;  // Slice of the source.
  gsize length;
  BosonogaValueKind kind;
  guint slot;
  gboolean is_val;
  // Owns a str that new made, which is unset in the block it is declared in,
  // or that a statement like io_read gave its block, which frees it after.
  gboolean is_new;
  const gchar *freed_by;  // The word of that statement, or NULL.
  gboolean is_array;
  guint function;  // Of an array, in functions.
  guint block_depth;
  guint loop_depth;
  BosonogaDeclaration declaration;
} BosonogaVariable;

// Indices in ops of jumps, patched at the end of the loop.
typedef struct {
  GArray *break_jumps;
  GArray *continue_jumps;
} BosonogaLoop;

typedef struct {
  BosonogaTokenizer tokenizer;
  GArray *outer_files;  // Of BosonogaOuterFile.
  GArray *scopes;       // Of BosonogaScope.
  guint scope;          // Of the code being read.
  GArray *ops;
  GArray *variables;
  GArray *constants;
  guint slot_count;
  guint block_depth;
  GArray *loops;  // Of BosonogaLoop, innermost last.
  BosonogaSection section;
  GArray *functions;
  GArray *function_declarations;  // Of BosonogaDeclaration, as functions.
  gboolean in_function;
  guint function;  // In functions, while in_function.
  guint delta_time_slot;
  guint key_slot;
  gboolean has_section[BOSONOGA_SECTION_COUNT];
  BosonogaProgram *program;
  const BosonogaSettings *settings;
  // While the value parsed is only read, as in info, so that it can be new.
  gboolean reads_new;
} BosonogaParser;

gboolean bosonoga_token_is_word(const BosonogaToken *token, const gchar *word);

gboolean bosonoga_token_is_numbered(const BosonogaToken *token,
                                    const gchar *prefix);

gboolean bosonoga_token_setting(const BosonogaParser *parser,
                                const BosonogaToken *token,
                                BosonogaValue *value);

// Of a value that bosonoga_token_setting found to be a font or icon.
void bosonoga_name_asset(BosonogaParser *parser, const BosonogaToken *token,
                         BosonogaValue *value);

// Like ret_0_i32, which is reserved but not a value, so that a call names
// the values that it takes.
gboolean bosonoga_token_is_result(const BosonogaToken *token);

// A word that ends with _fn and has more before it.
gboolean bosonoga_token_is_function_name(const BosonogaToken *token);

gboolean bosonoga_token_is_reserved(const BosonogaParser *parser,
                                    const BosonogaToken *token);

gboolean bosonoga_numbered_value(const BosonogaToken *token,
                                 const gchar *prefix, guint64 max,
                                 guint64 *value);

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

// The find functions find only what the code being read sees, and the
// _any ones also what it does not, so that no two names are the same.
const BosonogaVariable *bosonoga_find_variable(const BosonogaParser *parser,
                                               const BosonogaToken *token);
const BosonogaVariable *bosonoga_find_variable_any(const BosonogaParser *parser,
                                                   const BosonogaToken *token);

// After the declarations of the scope and of scopes inside it, and the pub
// ones also after those of the scope around it.
gboolean bosonoga_is_visible(const BosonogaParser *parser,
                             const BosonogaDeclaration *declaration);

// TRUE, unless the token names something that the code does not see, which
// then fails.
gboolean bosonoga_check_not_hidden(const BosonogaParser *parser,
                                   const BosonogaToken *token, GError **error);

// Fails if what is declared from first on is never used.
gboolean bosonoga_check_variables_used(const BosonogaParser *parser,
                                       guint first, GError **error);

gboolean bosonoga_fail_unused(const BosonogaParser *parser,
                              const BosonogaDeclaration *declaration,
                              const gchar *what, GError **error);

void bosonoga_use_variable(BosonogaParser *parser,
                           const BosonogaVariable *variable);

void bosonoga_use_constant(BosonogaParser *parser,
                           const BosonogaConstant *constant);

// Of a call or a len, of a function or an array, which may be a variable.
void bosonoga_use_function(BosonogaParser *parser, const BosonogaToken *token,
                           guint function);

void bosonoga_declare_name(BosonogaParser *parser,
                           BosonogaDeclaration *declaration,
                           const BosonogaToken *name);

void bosonoga_append_function(BosonogaParser *parser,
                              const BosonogaFunction *function,
                              const BosonogaToken *name);

// The first variable that is new, or only of those in the innermost loop.
const BosonogaVariable *bosonoga_find_new(const BosonogaParser *parser,
                                          gboolean in_loop);

void bosonoga_declare_variable(BosonogaParser *parser,
                               const BosonogaToken *name,
                               BosonogaValueKind kind, guint slot);

// The caller declares the variable after its initial value, which so cannot
// refer to it.
gboolean bosonoga_fail_unknown_variable(const BosonogaParser *parser,
                                        const BosonogaToken *name,
                                        GError **error);

gboolean bosonoga_parse_new_name(BosonogaParser *parser, BosonogaToken *name,
                                 GError **error);

gboolean bosonoga_at_value(const BosonogaParser *parser);

gboolean bosonoga_parse_value(BosonogaParser *parser, BosonogaOperand *operand,
                              GError **error);

gboolean bosonoga_parse_value_of_kind(BosonogaParser *parser,
                                      BosonogaValueKind kind,
                                      const gchar *context,
                                      BosonogaOperand *operand, GError **error);

const BosonogaConstant *bosonoga_find_constant(const BosonogaParser *parser,
                                               const BosonogaToken *token);
const BosonogaConstant *bosonoga_find_constant_any(const BosonogaParser *parser,
                                                   const BosonogaToken *token);
gboolean bosonoga_parse_constant(BosonogaParser *parser, GError **error);

gboolean bosonoga_parse_var(BosonogaParser *parser, gboolean is_val,
                            GError **error);

gboolean bosonoga_parse_set(BosonogaParser *parser, GError **error);

// The ops after the condition run if it is true, and the caller patches
// false_jumps to where to go if it is false.
gboolean bosonoga_parse_condition(BosonogaParser *parser, GArray *false_jumps,
                                  GError **error);

// An index in functions, or -1. Functions and arrays share one list.
gint bosonoga_find_function(const BosonogaParser *parser,
                            const BosonogaToken *token);
gint bosonoga_find_function_any(const BosonogaParser *parser,
                                const BosonogaToken *token);

const gchar *bosonoga_function_noun(const BosonogaFunction *function);

const gchar *bosonoga_function_gives(const BosonogaFunction *function);

gboolean bosonoga_check_new_function_name(const BosonogaParser *parser,
                                          const BosonogaToken *name,
                                          const gchar *what, GError **error);

// Like 1_str, an index from 0 without leading zeros, then _ and a type.
gboolean bosonoga_split_indexed(const gchar *text, gsize length, guint64 *index,
                                BosonogaValueKind *kind);

gboolean bosonoga_token_indexed(const BosonogaToken *token, guint64 *index,
                                BosonogaValueKind *kind);

gboolean bosonoga_parse_types(BosonogaParser *parser, BosonogaValueKind *kinds,
                              guint *count, const gchar *too_many,
                              GError **error);

gboolean bosonoga_parse_function(BosonogaParser *parser, GError **error);

gboolean bosonoga_parse_call(BosonogaParser *parser, guint *function,
                             GError **error);

gboolean bosonoga_check_single_result(const BosonogaParser *parser,
                                      const BosonogaToken *token, guint index,
                                      GError **error);

gboolean bosonoga_parse_taken_result(BosonogaParser *parser, guint index,
                                     GError **error);

// What a statement gives to the block after its with ret, as a function does.
typedef struct {
  const gchar *name;
  const gchar *gives;  // As in "'f_fn' returns", or "io_read gives".
  guint count;
  BosonogaValueKind kinds[BOSONOGA_FUNCTION_VALUES_MAX];
  // Others that may stand for kinds[i], as str for the xml_str of io_read.
  guint32 also[BOSONOGA_FUNCTION_VALUES_MAX];  // Bits of BosonogaValueKind.
  gboolean is_owned[BOSONOGA_FUNCTION_VALUES_MAX];  // Of a str it makes.
} BosonogaResults;

// The values that a block takes, by increasing index.
typedef struct {
  guint count;
  guint indices[BOSONOGA_FUNCTION_VALUES_MAX];
  BosonogaValueKind kinds[BOSONOGA_FUNCTION_VALUES_MAX];
  guint slots[BOSONOGA_FUNCTION_VALUES_MAX];
} BosonogaTaken;

// After with ret, up to the di of the block, as call ... with ret.
gboolean bosonoga_parse_results(BosonogaParser *parser,
                                const BosonogaToken *start,
                                const BosonogaResults *results,
                                BosonogaTaken *taken, GError **error);

gboolean bosonoga_parse_new_xml(BosonogaParser *parser,
                                const BosonogaToken *name, gboolean is_val,
                                GError **error);

gboolean bosonoga_parse_join(BosonogaParser *parser, const BosonogaToken *name,
                             const BosonogaVariable *target, GError **error);

// Of the word that starts io_read, io_write, io_modified, xml_child,
// xml_attribute or str_parse.
gboolean bosonoga_is_given_statement(const BosonogaToken *token);

gboolean bosonoga_parse_given_statement(BosonogaParser *parser,
                                        const BosonogaToken *start,
                                        GError **error);

gboolean bosonoga_parse_call_statement(BosonogaParser *parser,
                                       const BosonogaToken *start,
                                       GError **error);

gboolean bosonoga_parse_function_return(BosonogaParser *parser,
                                        const BosonogaToken *start,
                                        GError **error);

gchar *bosonoga_values_shown(const BosonogaValueKind *kinds, guint count);

gboolean bosonoga_parse_val_array(BosonogaParser *parser,
                                  const BosonogaToken *name, GError **error);

gboolean bosonoga_parse_var_array(BosonogaParser *parser,
                                  const BosonogaToken *name, GError **error);

gboolean bosonoga_parse_array_write(BosonogaParser *parser,
                                    const BosonogaToken *name,
                                    const BosonogaVariable *array,
                                    GError **error);

gboolean bosonoga_at_len(const BosonogaParser *parser);

gboolean bosonoga_parse_len(BosonogaParser *parser, BosonogaValue *value,
                            GError **error);

gboolean bosonoga_parse_array_read(BosonogaParser *parser,
                                   const BosonogaToken *name, guint found,
                                   GError **error);

gboolean bosonoga_at_conversion(const BosonogaParser *parser);

gboolean bosonoga_parse_conversion(BosonogaParser *parser, guint slot,
                                   BosonogaValueKind *kind, GError **error);

gboolean bosonoga_parse_block(BosonogaParser *parser, GError **error);

gboolean bosonoga_parse_if(BosonogaParser *parser, GError **error);

gboolean bosonoga_parse_loop(BosonogaParser *parser, GError **error);

gboolean bosonoga_parse_while(BosonogaParser *parser,
                              const BosonogaToken *start, GError **error);

gboolean bosonoga_parse_loop_jump(BosonogaParser *parser,
                                  const BosonogaToken *start, GError **error);

void bosonoga_loop_clear(gpointer loop);

#endif  // BOSONOGA_PARSER_INTERNAL_H_
