#include "parser.h"

#include <stdarg.h>
#include <string.h>

#include "daria.h"
#include "tokenizer.h"

#define DARIA_MESSAGE_TOKEN_MAX 32
#define DARIA_BLOCK_DEPTH_MAX 256
#define DARIA_SHIFT_PREFIX "shift_"
#define DARIA_SHIFT_BIT_MAX 31
#define DARIA_LOOP_PREFIX "to_"

static const gchar *const daria_reserved_words[] = {
    "main",        "di",           "do",           "info",     "var",
    "set",         "is",           "if",           "else",     "and",
    "or",          "gt",           "lt",           "eq",       "not_eq",
    "eq_3",        "eq_6",         "not_eq_3",     "not_eq_6", "nihil",
    "return",      "exit_success", "exit_failure", "with",     "to_add",
    "to_subtract", "to_f32",       "to_i32",       "shift_or", "shift_has",
    "sleep_ms",    "gfx_init",     "gfx_frame",    "gfx_main", "gfx_cleanup",
};

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
  gboolean in_callback;  // Whether the cursor is in a callback section.
  gboolean has_section[DARIA_SECTION_COUNT];
  DariaProgram *program;
} DariaParser;

static gboolean daria_parse_statement(DariaParser *parser, GError **error);

static gboolean daria_token_is_word(const DariaToken *token,
                                    const gchar *word) {
  return token->kind == DARIA_TOKEN_WORD && token->length == strlen(word) &&
         memcmp(token->text, word, token->length) == 0;
}

// Returns whether the token is a word made of the prefix and
// at least one digit.
static gboolean daria_token_is_numbered(const DariaToken *token,
                                        const gchar *prefix) {
  const gsize prefix_length = strlen(prefix);
  if (token->kind != DARIA_TOKEN_WORD || token->length <= prefix_length ||
      memcmp(token->text, prefix, prefix_length) != 0) {
    return FALSE;
  }
  for (gsize i = prefix_length; i < token->length; ++i) {
    if (!g_ascii_isdigit(token->text[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean daria_token_is_reserved(const DariaToken *token) {
  for (gsize i = 0; i < G_N_ELEMENTS(daria_reserved_words); ++i) {
    if (daria_token_is_word(token, daria_reserved_words[i])) {
      return TRUE;
    }
  }
  return daria_token_is_numbered(token, DARIA_SHIFT_PREFIX) ||
         daria_token_is_numbered(token, DARIA_LOOP_PREFIX);
}

static const gchar *daria_value_kind_name(DariaValueKind kind) {
  switch (kind) {
    case DARIA_VALUE_I32:
      return "i32";
    case DARIA_VALUE_F32:
      return "f32";
    case DARIA_VALUE_FLAGS:
      return "flags";
  }
  g_return_val_if_reached("unknown");
}

// The source is arbitrary bytes and the cut can split a character, while GError
// messages must be UTF-8.
static gchar *daria_token_shown(const DariaToken *token) {
  return g_utf8_make_valid(token->text,
                           (gssize)MIN(token->length, DARIA_MESSAGE_TOKEN_MAX));
}

static gboolean daria_fail_at(const DariaParser *parser,
                              const DariaToken *token, GError **error,
                              DariaError code, const gchar *format, ...)
    G_GNUC_PRINTF(5, 6);

static gboolean daria_fail_at(const DariaParser *parser,
                              const DariaToken *token, GError **error,
                              DariaError code, const gchar *format, ...) {
  va_list args;
  va_start(args, format);
  g_autofree gchar *message = g_strdup_vprintf(format, args);
  va_end(args);
  g_set_error(error, DARIA_ERROR, (gint)code,
              "%s:%" G_GSIZE_FORMAT ":%" G_GSIZE_FORMAT ": %s",
              parser->tokenizer.path, token->line, token->column, message);
  return FALSE;
}

static gboolean daria_parse_error(const DariaParser *parser,
                                  const gchar *expected, GError **error) {
  const DariaToken *token = &parser->tokenizer.token;
  if (token->kind == DARIA_TOKEN_END) {
    return daria_fail_at(parser, token, error, DARIA_ERROR_SYNTAX,
                         "expected %s, got end of file", expected);
  }
  g_autofree gchar *shown = daria_token_shown(token);
  return daria_fail_at(parser, token, error, DARIA_ERROR_SYNTAX,
                       "expected %s, got '%s'", expected, shown);
}

static gboolean daria_advance(DariaParser *parser, GError **error) {
  return daria_tokenizer_advance(&parser->tokenizer, error);
}

static gboolean daria_at_word(const DariaParser *parser, const gchar *word) {
  return daria_token_is_word(&parser->tokenizer.token, word);
}

static gboolean daria_expect_word(DariaParser *parser, const gchar *word,
                                  GError **error) {
  if (!daria_at_word(parser, word)) {
    return daria_parse_error(parser, word, error);
  }
  return daria_advance(parser, error);
}

// Appends a zeroed op located at the token. The pointer is valid until the next
// op is emitted.
static DariaOp *daria_emit(DariaParser *parser, DariaOpKind kind,
                           const DariaToken *token) {
  g_array_set_size(parser->ops, parser->ops->len + 1);
  DariaOp *op = &g_array_index(parser->ops, DariaOp, parser->ops->len - 1);
  op->kind = kind;
  op->line = token->line;
  op->column = token->column;
  return op;
}

static void daria_patch_jumps_to_here(DariaParser *parser,
                                      const GArray *jumps) {
  for (guint i = 0; i < jumps->len; ++i) {
    const guint jump = g_array_index(jumps, guint, i);
    g_array_index(parser->ops, DariaOp, jump).target = parser->ops->len;
  }
}

static const DariaVariable *daria_find_variable(const DariaParser *parser,
                                                const DariaToken *token) {
  for (guint i = 0; i < parser->variables->len; ++i) {
    const DariaVariable *variable =
        &g_array_index(parser->variables, DariaVariable, i);
    if (variable->length == token->length &&
        memcmp(variable->name, token->text, token->length) == 0) {
      return variable;
    }
  }
  return NULL;
}

static void daria_declare_variable(DariaParser *parser, const DariaToken *name,
                                   DariaValueKind kind, guint slot) {
  const DariaVariable variable = {name->text, name->length, kind, slot};
  g_array_append_val(parser->variables, variable);
}

// Reads the name of a new variable, which the caller declares once its initial
// value is parsed, so that the value cannot refer to the variable.
static gboolean daria_parse_new_name(DariaParser *parser, DariaToken *name,
                                     GError **error) {
  *name = parser->tokenizer.token;
  if (name->kind != DARIA_TOKEN_WORD || daria_token_is_reserved(name)) {
    return daria_parse_error(parser, "a variable name", error);
  }
  if (daria_find_variable(parser, name) != NULL) {
    g_autofree gchar *shown = daria_token_shown(name);
    return daria_fail_at(parser, name, error, DARIA_ERROR_NAME,
                         "variable '%s' already exists", shown);
  }
  return daria_advance(parser, error);
}

// Reads the digits after the prefix of a word that
// daria_token_is_numbered accepted.
static gboolean daria_numbered_value(const DariaToken *token,
                                     const gchar *prefix, guint64 max,
                                     guint64 *value) {
  const gsize prefix_length = strlen(prefix);
  g_autofree gchar *digits =
      g_strndup(token->text + prefix_length, token->length - prefix_length);
  return g_ascii_string_to_unsigned(digits, 10, 0, max, value, NULL);
}

static gboolean daria_at_value(const DariaParser *parser) {
  const DariaToken *token = &parser->tokenizer.token;
  switch (token->kind) {
    case DARIA_TOKEN_INTEGER:
    case DARIA_TOKEN_REAL:
      return TRUE;
    case DARIA_TOKEN_WORD:
      return !daria_token_is_reserved(token) ||
             daria_token_is_numbered(token, DARIA_SHIFT_PREFIX);
    case DARIA_TOKEN_END:
    case DARIA_TOKEN_UNSIGNED:
    case DARIA_TOKEN_STRING:
      return FALSE;
  }
  g_return_val_if_reached(FALSE);
}

static gboolean daria_parse_value(DariaParser *parser, DariaOperand *operand,
                                  GError **error) {
  const DariaToken *token = &parser->tokenizer.token;
  if (!daria_at_value(parser)) {
    return daria_parse_error(parser, "a value", error);
  }

  memset(operand, 0, sizeof(*operand));
  if (token->kind == DARIA_TOKEN_INTEGER) {
    operand->literal.kind = DARIA_VALUE_I32;
    operand->literal.i32 = token->integer;
  } else if (token->kind == DARIA_TOKEN_REAL) {
    operand->literal.kind = DARIA_VALUE_F32;
    operand->literal.f32 = token->real;
  } else if (daria_token_is_numbered(token, DARIA_SHIFT_PREFIX)) {
    guint64 bit = 0;
    if (!daria_numbered_value(token, DARIA_SHIFT_PREFIX, DARIA_SHIFT_BIT_MAX,
                              &bit)) {
      g_autofree gchar *shown = daria_token_shown(token);
      return daria_fail_at(parser, token, error, DARIA_ERROR_SYNTAX,
                           "'%s' is out of range, the last bit is %s%d", shown,
                           DARIA_SHIFT_PREFIX, DARIA_SHIFT_BIT_MAX);
    }
    operand->literal.kind = DARIA_VALUE_FLAGS;
    operand->literal.flags = 1U << bit;
  } else {
    const DariaVariable *variable = daria_find_variable(parser, token);
    if (variable == NULL) {
      g_autofree gchar *shown = daria_token_shown(token);
      return daria_fail_at(parser, token, error, DARIA_ERROR_NAME,
                           "unknown variable '%s'", shown);
    }
    operand->literal.kind = variable->kind;
    operand->is_variable = TRUE;
    operand->slot = variable->slot;
  }
  return daria_advance(parser, error);
}

// Parses a value that must have the kind that the context expects.
static gboolean daria_parse_value_of_kind(DariaParser *parser,
                                          DariaValueKind kind,
                                          const gchar *context,
                                          DariaOperand *operand,
                                          GError **error) {
  const DariaToken token = parser->tokenizer.token;
  if (!daria_parse_value(parser, operand, error)) {
    return FALSE;
  }
  if (operand->literal.kind != kind) {
    g_autofree gchar *shown = daria_token_shown(&token);
    return daria_fail_at(parser, &token, error, DARIA_ERROR_TYPE,
                         "%s needs %s, but '%s' is %s", context,
                         daria_value_kind_name(kind), shown,
                         daria_value_kind_name(operand->literal.kind));
  }
  return TRUE;
}

// Parses the arguments of an info statement, whose keyword is already consumed.
static gboolean daria_parse_info(DariaParser *parser, const DariaToken *start,
                                 GError **error) {
  const DariaToken *token = &parser->tokenizer.token;
  if (token->kind != DARIA_TOKEN_STRING && !daria_at_value(parser)) {
    return daria_parse_error(parser, "a value or string", error);
  }

  while (token->kind == DARIA_TOKEN_STRING || daria_at_value(parser)) {
    if (token->kind == DARIA_TOKEN_STRING) {
      DariaOp *op = daria_emit(parser, DARIA_OP_PRINT_STRING, token);
      op->text = token->text + 1;
      op->length = token->length - 2;
      if (!daria_advance(parser, error)) {
        return FALSE;
      }
    } else {
      const DariaToken value_token = *token;
      DariaOperand value;
      if (!daria_parse_value(parser, &value, error)) {
        return FALSE;
      }
      daria_emit(parser, DARIA_OP_PRINT_VALUE, &value_token)->left = value;
    }
  }

  daria_emit(parser, DARIA_OP_PRINT_NEWLINE, start);
  return TRUE;
}

// Parses a conversion and stores its result in the slot. The keyword is the
// current token, and the kind of the result is returned in kind.
static gboolean daria_parse_conversion(DariaParser *parser, guint slot,
                                       DariaValueKind *kind, GError **error) {
  const DariaToken start = parser->tokenizer.token;
  const gboolean to_f32 = daria_at_word(parser, "to_f32");
  DariaOperand source;
  if (!daria_advance(parser, error) ||
      !daria_parse_value_of_kind(
          parser, to_f32 ? DARIA_VALUE_I32 : DARIA_VALUE_F32,
          to_f32 ? "to_f32" : "to_i32", &source, error)) {
    return FALSE;
  }
  DariaOp *op = daria_emit(
      parser, to_f32 ? DARIA_OP_CONVERT_TO_F32 : DARIA_OP_CONVERT_TO_I32,
      &start);
  op->slot = slot;
  op->left = source;
  *kind = to_f32 ? DARIA_VALUE_F32 : DARIA_VALUE_I32;
  return TRUE;
}

static gboolean daria_at_conversion(const DariaParser *parser) {
  return daria_at_word(parser, "to_f32") || daria_at_word(parser, "to_i32");
}

// Parses a var statement, whose keyword is already consumed.
static gboolean daria_parse_var(DariaParser *parser, GError **error) {
  DariaToken name;
  if (!daria_parse_new_name(parser, &name, error) ||
      !daria_expect_word(parser, "is", error)) {
    return FALSE;
  }

  const guint slot = parser->slot_count++;
  DariaValueKind kind = DARIA_VALUE_I32;
  if (daria_at_conversion(parser)) {
    if (!daria_parse_conversion(parser, slot, &kind, error)) {
      return FALSE;
    }
  } else {
    const DariaToken start = parser->tokenizer.token;
    DariaOperand initial;
    if (!daria_parse_value(parser, &initial, error)) {
      return FALSE;
    }
    kind = initial.literal.kind;
    DariaOp *op = daria_emit(parser, DARIA_OP_ASSIGN, &start);
    op->slot = slot;
    op->left = initial;

    while (kind == DARIA_VALUE_FLAGS && daria_at_word(parser, "shift_or")) {
      const DariaToken shift_or = parser->tokenizer.token;
      DariaOperand flags;
      if (!daria_advance(parser, error) ||
          !daria_parse_value_of_kind(parser, DARIA_VALUE_FLAGS, "shift_or",
                                     &flags, error)) {
        return FALSE;
      }
      op = daria_emit(parser, DARIA_OP_FLAGS_OR, &shift_or);
      op->slot = slot;
      op->left = flags;
    }
  }

  daria_declare_variable(parser, &name, kind, slot);
  return TRUE;
}

// Parses the values of to_add or to_subtract, whose keyword is
// alreadyconsumed.
static gboolean daria_parse_accumulation(DariaParser *parser,
                                         const DariaVariable *target,
                                         const DariaToken *start,
                                         gboolean subtract, GError **error) {
  const gchar *context = subtract ? "to_subtract" : "to_add";
  const DariaOpKind step =
      subtract ? DARIA_OP_ACCUMULATOR_SUBTRACT : DARIA_OP_ACCUMULATOR_ADD;
  const DariaValueKind kind = target->kind;
  const guint slot = target->slot;

  DariaOperand first;
  if (!daria_parse_value_of_kind(parser, kind, context, &first, error)) {
    return FALSE;
  }
  if (subtract && !daria_at_value(parser)) {
    DariaOperand zero;
    memset(&zero, 0, sizeof(zero));
    zero.literal.kind = kind;
    daria_emit(parser, DARIA_OP_ACCUMULATOR_LOAD, start)->left = zero;
    daria_emit(parser, step, start)->left = first;
  } else {
    daria_emit(parser, DARIA_OP_ACCUMULATOR_LOAD, start)->left = first;
  }

  while (daria_at_value(parser)) {
    DariaOperand next;
    if (!daria_parse_value_of_kind(parser, kind, context, &next, error)) {
      return FALSE;
    }
    daria_emit(parser, step, start)->left = next;
  }
  daria_emit(parser, DARIA_OP_ACCUMULATOR_STORE, start)->slot = slot;
  return TRUE;
}

// Parses a set statement, whose keyword is already consumed.
static gboolean daria_parse_set(DariaParser *parser, GError **error) {
  const DariaToken name = parser->tokenizer.token;
  if (name.kind != DARIA_TOKEN_WORD || daria_token_is_reserved(&name)) {
    return daria_parse_error(parser, "a variable name", error);
  }
  g_autofree gchar *shown = daria_token_shown(&name);
  const DariaVariable *found = daria_find_variable(parser, &name);
  if (found == NULL) {
    return daria_fail_at(parser, &name, error, DARIA_ERROR_NAME,
                         "unknown variable '%s'", shown);
  }
  const DariaVariable target = *found;
  if (!daria_advance(parser, error)) {
    return FALSE;
  }

  const DariaToken operation = parser->tokenizer.token;
  if (daria_at_conversion(parser)) {
    DariaValueKind kind = DARIA_VALUE_I32;
    if (!daria_parse_conversion(parser, target.slot, &kind, error)) {
      return FALSE;
    }
    if (kind != target.kind) {
      return daria_fail_at(parser, &operation, error, DARIA_ERROR_TYPE,
                           "'%s' is %s, but the conversion gives %s", shown,
                           daria_value_kind_name(target.kind),
                           daria_value_kind_name(kind));
    }
    return TRUE;
  }

  const gboolean subtract = daria_at_word(parser, "to_subtract");
  if (!subtract && !daria_at_word(parser, "to_add")) {
    return daria_parse_error(parser, "to_add, to_subtract, to_f32 or to_i32",
                             error);
  }
  if (target.kind == DARIA_VALUE_FLAGS) {
    return daria_fail_at(parser, &operation, error, DARIA_ERROR_TYPE,
                         "'%s' is flags, which cannot be added or subtracted",
                         shown);
  }
  return daria_advance(parser, error) &&
         daria_parse_accumulation(parser, &target, &name, subtract, error);
}

static gboolean daria_parse_compare_kind(const DariaParser *parser,
                                         DariaCompareKind *compare,
                                         GError **error) {
  static const struct {
    const gchar *word;
    DariaCompareKind kind;
  } compares[] = {
      {"gt", DARIA_COMPARE_GT},
      {"lt", DARIA_COMPARE_LT},
      {"eq", DARIA_COMPARE_EQ},
      {"not_eq", DARIA_COMPARE_NOT_EQ},
      {"eq_3", DARIA_COMPARE_EQ_3},
      {"eq_6", DARIA_COMPARE_EQ_6},
      {"not_eq_3", DARIA_COMPARE_NOT_EQ_3},
      {"not_eq_6", DARIA_COMPARE_NOT_EQ_6},
      {"shift_has", DARIA_COMPARE_SHIFT_HAS},
  };
  for (gsize i = 0; i < G_N_ELEMENTS(compares); ++i) {
    if (daria_at_word(parser, compares[i].word)) {
      *compare = compares[i].kind;
      return TRUE;
    }
  }
  return daria_parse_error(parser, "a comparison", error);
}

// Checks that the comparison, located at the token, accepts the operand kinds.
static gboolean daria_check_compare(const DariaParser *parser,
                                    const DariaToken *token,
                                    DariaCompareKind compare,
                                    DariaValueKind left, DariaValueKind right,
                                    GError **error) {
  g_autofree gchar *shown = daria_token_shown(token);
  if (compare == DARIA_COMPARE_SHIFT_HAS) {
    if (left == DARIA_VALUE_FLAGS && right == DARIA_VALUE_FLAGS) {
      return TRUE;
    }
    return daria_fail_at(parser, token, error, DARIA_ERROR_TYPE,
                         "shift_has needs flags, not %s and %s",
                         daria_value_kind_name(left),
                         daria_value_kind_name(right));
  }
  if (left != right) {
    return daria_fail_at(
        parser, token, error, DARIA_ERROR_TYPE, "cannot compare %s with %s",
        daria_value_kind_name(left), daria_value_kind_name(right));
  }
  if (left == DARIA_VALUE_FLAGS) {
    return daria_fail_at(parser, token, error, DARIA_ERROR_TYPE,
                         "flags can only be compared with shift_has");
  }

  switch (compare) {
    case DARIA_COMPARE_GT:
    case DARIA_COMPARE_LT:
      return TRUE;
    case DARIA_COMPARE_EQ:
    case DARIA_COMPARE_NOT_EQ:
      if (left == DARIA_VALUE_F32) {
        return daria_fail_at(parser, token, error, DARIA_ERROR_TYPE,
                             "f32 has no exact %s, use %s_3 or %s_6", shown,
                             shown, shown);
      }
      return TRUE;
    case DARIA_COMPARE_EQ_3:
    case DARIA_COMPARE_EQ_6:
    case DARIA_COMPARE_NOT_EQ_3:
    case DARIA_COMPARE_NOT_EQ_6:
      if (left == DARIA_VALUE_I32) {
        return daria_fail_at(parser, token, error, DARIA_ERROR_TYPE,
                             "%s only works on f32, i32 uses eq or not_eq",
                             shown);
      }
      return TRUE;
    case DARIA_COMPARE_SHIFT_HAS:
      break;
  }
  g_return_val_if_reached(FALSE);
}

// Parses a comparison into a jump that is taken if it is false, and appends the
// index of the jump to false_jumps.
static gboolean daria_parse_comparison(DariaParser *parser, GArray *false_jumps,
                                       GError **error) {
  DariaOperand left;
  DariaOperand right;
  DariaCompareKind compare = DARIA_COMPARE_EQ;
  if (!daria_parse_value(parser, &left, error) ||
      !daria_parse_compare_kind(parser, &compare, error)) {
    return FALSE;
  }
  const DariaToken operation = parser->tokenizer.token;
  if (!daria_advance(parser, error) ||
      !daria_parse_value(parser, &right, error) ||
      !daria_check_compare(parser, &operation, compare, left.literal.kind,
                           right.literal.kind, error)) {
    return FALSE;
  }

  const guint jump = parser->ops->len;
  DariaOp *op = daria_emit(parser, DARIA_OP_JUMP_UNLESS, &operation);
  op->compare = compare;
  op->left = left;
  op->right = right;
  g_array_append_val(false_jumps, jump);
  return TRUE;
}

// Parses a condition, leaving the ops right after it as what runs if it is
// true. The caller patches false_jumps to what runs if it is false.
//
// Each group of "and" jumps to the next group as soon as a comparison is false,
// and to the true case when it reaches its end, so that "and" binds tighter.
static gboolean daria_parse_condition(DariaParser *parser, GArray *false_jumps,
                                      GError **error) {
  g_autoptr(GArray) true_jumps = g_array_new(FALSE, FALSE, sizeof(guint));
  while (TRUE) {
    if (!daria_parse_comparison(parser, false_jumps, error)) {
      return FALSE;
    }
    if (daria_at_word(parser, "and")) {
      if (!daria_advance(parser, error)) {
        return FALSE;
      }
    } else if (daria_at_word(parser, "or")) {
      const guint jump = parser->ops->len;
      daria_emit(parser, DARIA_OP_JUMP, &parser->tokenizer.token);
      g_array_append_val(true_jumps, jump);
      daria_patch_jumps_to_here(parser, false_jumps);
      g_array_set_size(false_jumps, 0);
      if (!daria_advance(parser, error)) {
        return FALSE;
      }
    } else {
      daria_patch_jumps_to_here(parser, true_jumps);
      return TRUE;
    }
  }
}

static gboolean daria_parse_block(DariaParser *parser, GError **error) {
  const DariaToken opening = parser->tokenizer.token;
  if (!daria_expect_word(parser, "di", error)) {
    return FALSE;
  }
  if (daria_at_word(parser, "do")) {
    return daria_fail_at(parser, &opening, error, DARIA_ERROR_SYNTAX,
                         "empty block, write nihil for a block that does "
                         "nothing");
  }
  // The parser recurses for each nested block, so the depth must be bounded.
  if (parser->block_depth == DARIA_BLOCK_DEPTH_MAX) {
    return daria_fail_at(parser, &opening, error, DARIA_ERROR_SYNTAX,
                         "blocks are nested more than %d levels deep",
                         DARIA_BLOCK_DEPTH_MAX);
  }

  ++parser->block_depth;
  const guint visible_count = parser->variables->len;
  while (!daria_at_word(parser, "do") &&
         parser->tokenizer.token.kind != DARIA_TOKEN_END) {
    if (!daria_parse_statement(parser, error)) {
      return FALSE;
    }
  }
  g_array_set_size(parser->variables, visible_count);
  --parser->block_depth;
  return daria_expect_word(parser, "do", error);
}

// Parses the block of a callback section, which cannot return.
static gboolean daria_parse_callback(DariaParser *parser, GError **error) {
  parser->in_callback = TRUE;
  const gboolean parsed = daria_parse_block(parser, error);
  parser->in_callback = FALSE;
  return parsed;
}

// Parses an if statement, whose keyword is already consumed.
static gboolean daria_parse_if(DariaParser *parser, GError **error) {
  g_autoptr(GArray) exit_jumps = g_array_new(FALSE, FALSE, sizeof(guint));
  g_autoptr(GArray) false_jumps = g_array_new(FALSE, FALSE, sizeof(guint));
  gboolean has_condition = TRUE;
  while (has_condition) {
    g_array_set_size(false_jumps, 0);
    if (!daria_parse_condition(parser, false_jumps, error) ||
        !daria_parse_block(parser, error)) {
      return FALSE;
    }
    if (!daria_at_word(parser, "else")) {
      daria_patch_jumps_to_here(parser, false_jumps);
      break;
    }

    const guint jump = parser->ops->len;
    daria_emit(parser, DARIA_OP_JUMP, &parser->tokenizer.token);
    g_array_append_val(exit_jumps, jump);
    daria_patch_jumps_to_here(parser, false_jumps);
    if (!daria_advance(parser, error)) {
      return FALSE;
    }

    has_condition = daria_at_word(parser, "if");
    if (has_condition) {
      if (!daria_advance(parser, error)) {
        return FALSE;
      }
    } else if (!daria_parse_block(parser, error)) {
      return FALSE;
    }
  }
  daria_patch_jumps_to_here(parser, exit_jumps);
  return TRUE;
}

// Parses a loop, whose keyword is the current token. The body works on a copy
// of the hidden counter, so setting the variable does not change the
// iterations.
static gboolean daria_parse_loop(DariaParser *parser, GError **error) {
  const DariaToken start = parser->tokenizer.token;
  guint64 limit = 0;
  if (!daria_numbered_value(&start, DARIA_LOOP_PREFIX, G_MAXINT32, &limit)) {
    g_autofree gchar *shown = daria_token_shown(&start);
    return daria_fail_at(parser, &start, error, DARIA_ERROR_SYNTAX,
                         "'%s' does not fit in 32 bits", shown);
  }
  if (!daria_advance(parser, error)) {
    return FALSE;
  }

  DariaToken name;
  const gboolean has_name = daria_at_word(parser, "with");
  if (has_name) {
    if (!daria_advance(parser, error) ||
        !daria_parse_new_name(parser, &name, error)) {
      return FALSE;
    }
  }

  const guint counter = parser->slot_count++;
  DariaOp *op = daria_emit(parser, DARIA_OP_ASSIGN, &start);
  op->slot = counter;
  op->left.literal.kind = DARIA_VALUE_I32;

  const guint test = parser->ops->len;
  op = daria_emit(parser, DARIA_OP_LOOP_TEST, &start);
  op->slot = counter;
  op->integer = (gint32)limit;

  const guint visible_count = parser->variables->len;
  if (has_name) {
    const guint slot = parser->slot_count++;
    op = daria_emit(parser, DARIA_OP_ASSIGN, &start);
    op->slot = slot;
    op->left.literal.kind = DARIA_VALUE_I32;
    op->left.is_variable = TRUE;
    op->left.slot = counter;
    daria_declare_variable(parser, &name, DARIA_VALUE_I32, slot);
  }

  if (!daria_parse_block(parser, error)) {
    return FALSE;
  }
  g_array_set_size(parser->variables, visible_count);

  op = daria_emit(parser, DARIA_OP_LOOP_NEXT, &start);
  op->slot = counter;
  op->target = test;
  g_array_index(parser->ops, DariaOp, test).target = parser->ops->len;
  return TRUE;
}

// Parses a sleep_ms statement, whose keyword is already consumed.
static gboolean daria_parse_sleep(DariaParser *parser, const DariaToken *start,
                                  GError **error) {
  const DariaToken *token = &parser->tokenizer.token;
  if (token->kind != DARIA_TOKEN_UNSIGNED) {
    return daria_parse_error(parser, "a _u32 number of milliseconds", error);
  }
  daria_emit(parser, DARIA_OP_SLEEP, start)->milliseconds =
      token->unsigned_integer;
  return daria_advance(parser, error);
}

// Parses a return statement, whose keyword is already consumed.
static gboolean daria_parse_return(DariaParser *parser, const DariaToken *start,
                                   GError **error) {
  if (parser->in_callback) {
    return daria_fail_at(parser, start, error, DARIA_ERROR_SYNTAX,
                         "only main and gfx_main can return");
  }
  gint32 status = DARIA_EXIT_SUCCESS;
  if (daria_at_word(parser, "exit_failure")) {
    status = DARIA_EXIT_FAILURE;
  } else if (!daria_at_word(parser, "exit_success")) {
    return daria_parse_error(parser, "exit_success or exit_failure", error);
  }
  daria_emit(parser, DARIA_OP_RETURN, start)->integer = status;
  return daria_advance(parser, error);
}

static gboolean daria_parse_statement(DariaParser *parser, GError **error) {
  const DariaToken start = parser->tokenizer.token;
  if (daria_token_is_numbered(&start, DARIA_LOOP_PREFIX)) {
    return daria_parse_loop(parser, error);
  }
  if (!daria_token_is_word(&start, "info") &&
      !daria_token_is_word(&start, "var") &&
      !daria_token_is_word(&start, "set") &&
      !daria_token_is_word(&start, "if") &&
      !daria_token_is_word(&start, "nihil") &&
      !daria_token_is_word(&start, "sleep_ms") &&
      !daria_token_is_word(&start, "return")) {
    return daria_parse_error(parser, "a statement", error);
  }
  if (!daria_advance(parser, error)) {
    return FALSE;
  }

  if (daria_token_is_word(&start, "info")) {
    return daria_parse_info(parser, &start, error);
  }
  if (daria_token_is_word(&start, "var")) {
    return daria_parse_var(parser, error);
  }
  if (daria_token_is_word(&start, "set")) {
    return daria_parse_set(parser, error);
  }
  if (daria_token_is_word(&start, "if")) {
    return daria_parse_if(parser, error);
  }
  if (daria_token_is_word(&start, "sleep_ms")) {
    return daria_parse_sleep(parser, &start, error);
  }
  if (daria_token_is_word(&start, "return")) {
    return daria_parse_return(parser, &start, error);
  }
  return TRUE;  // nihil
}

// Parses a program that does not start with main, so it must be a gfx program.
static gboolean daria_parse_gfx_sections(DariaParser *parser, GError **error) {
  static const struct {
    const gchar *word;
    DariaSection section;
  } sections[] = {
      {"gfx_init", DARIA_SECTION_GFX_INIT},
      {"gfx_frame", DARIA_SECTION_GFX_FRAME},
      {"gfx_cleanup", DARIA_SECTION_GFX_CLEANUP},
      {"gfx_main", DARIA_SECTION_MAIN},
  };
  const gchar *expected = "main, gfx_init, gfx_frame, gfx_cleanup or gfx_main";
  do {
    gsize i = 0;
    while (i < G_N_ELEMENTS(sections) &&
           !daria_at_word(parser, sections[i].word)) {
      ++i;
    }
    if (i == G_N_ELEMENTS(sections)) {
      return daria_parse_error(parser, expected, error);
    }
    expected = "gfx_init, gfx_frame, gfx_cleanup or gfx_main";

    const DariaSection section = sections[i].section;
    if (parser->has_section[section]) {
      return daria_fail_at(parser, &parser->tokenizer.token, error,
                           DARIA_ERROR_SYNTAX, "there is already a %s block",
                           sections[i].word);
    }
    parser->has_section[section] = TRUE;
    parser->program->entries[section] = parser->ops->len;
    if (!daria_advance(parser, error)) {
      return FALSE;
    }

    const gboolean is_main = section == DARIA_SECTION_MAIN;
    if (!(is_main ? daria_parse_block(parser, error)
                  : daria_parse_callback(parser, error))) {
      return FALSE;
    }
    daria_emit(parser, is_main ? DARIA_OP_MISSING_RETURN : DARIA_OP_SECTION_END,
               &parser->tokenizer.token);
  } while (parser->tokenizer.token.kind != DARIA_TOKEN_END);

  if (!parser->has_section[DARIA_SECTION_MAIN]) {
    return daria_fail_at(parser, &parser->tokenizer.token, error,
                         DARIA_ERROR_SYNTAX,
                         "a gfx program needs a gfx_main block");
  }
  parser->program->is_gfx = TRUE;
  return TRUE;
}

static gboolean daria_parse_program(DariaParser *parser, GError **error) {
  if (!daria_advance(parser, error)) {
    return FALSE;
  }
  if (daria_at_word(parser, "main")) {
    parser->has_section[DARIA_SECTION_MAIN] = TRUE;
    if (!daria_advance(parser, error) || !daria_parse_block(parser, error)) {
      return FALSE;
    }
    if (parser->tokenizer.token.kind != DARIA_TOKEN_END) {
      return daria_parse_error(parser, "end of file", error);
    }
    daria_emit(parser, DARIA_OP_MISSING_RETURN, &parser->tokenizer.token);
  } else if (!daria_parse_gfx_sections(parser, error)) {
    return FALSE;
  }

  for (guint section = 0; section < DARIA_SECTION_COUNT; ++section) {
    if (!parser->has_section[section]) {
      parser->program->entries[section] = parser->ops->len;
      daria_emit(parser, DARIA_OP_SECTION_END, &parser->tokenizer.token);
    }
  }
  return TRUE;
}

void daria_program_free(DariaProgram *program) {
  g_array_unref(program->ops);
  g_free(program);
}

DariaProgram *daria_parse(const gchar *path, const gchar *source, gsize length,
                          GError **error) {
  g_autoptr(GArray) variables =
      g_array_new(FALSE, FALSE, sizeof(DariaVariable));
  g_autoptr(DariaProgram) program = g_new0(DariaProgram, 1);
  program->path = path;
  program->ops = g_array_new(FALSE, TRUE, sizeof(DariaOp));

  DariaParser parser;
  memset(&parser, 0, sizeof(parser));
  daria_tokenizer_init(&parser.tokenizer, path, source, length);
  parser.ops = program->ops;
  parser.variables = variables;
  parser.program = program;
  if (!daria_parse_program(&parser, error)) {
    return NULL;
  }
  program->slot_count = parser.slot_count;
  return g_steal_pointer(&program);
}
