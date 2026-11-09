#include <string.h>

#include "parser/internal.h"

gint bosonoga_find_function(const BosonogaParser *parser,
                            const BosonogaToken *token) {
  if (token->kind != BOSONOGA_TOKEN_WORD) {
    return -1;
  }
  for (guint i = 0; i < parser->functions->len; ++i) {
    const BosonogaFunction *function =
        &g_array_index(parser->functions, BosonogaFunction, i);
    if (function->name_length == token->length &&
        memcmp(function->name, token->text, token->length) == 0) {
      return (gint)i;
    }
  }
  return -1;
}

const gchar *bosonoga_function_noun(const BosonogaFunction *function) {
  return function->is_array ? "array" : "function";
}

const gchar *bosonoga_function_gives(const BosonogaFunction *function) {
  return function->is_array ? "has rows of" : "returns";
}

gboolean bosonoga_check_new_function_name(const BosonogaParser *parser,
                                          const BosonogaToken *name,
                                          const gchar *what, GError **error) {
  if (name->kind != BOSONOGA_TOKEN_WORD ||
      bosonoga_token_is_reserved(parser, name)) {
    return bosonoga_parse_error(parser, what, error);
  }
  g_autofree gchar *shown = bosonoga_token_shown(name);
  const gint found = bosonoga_find_function(parser, name);
  if (found >= 0) {
    return bosonoga_fail_at(
        parser, name, error, BOSONOGA_ERROR_NAME, "%s '%s' already exists",
        bosonoga_function_noun(
            &g_array_index(parser->functions, BosonogaFunction, (guint)found)),
        shown);
  }
  if (bosonoga_find_variable(parser, name) != NULL) {
    return bosonoga_fail_at(parser, name, error, BOSONOGA_ERROR_NAME,
                            "'%s' is already a variable", shown);
  }
  return TRUE;
}

gchar *bosonoga_values_shown(const BosonogaValueKind *kinds, guint count) {
  if (count == 0) {
    return g_strdup("no values");
  }
  GString *shown = g_string_new(NULL);
  g_string_append_printf(shown, "%u %s,", count,
                         count == 1 ? "value" : "values");
  for (guint i = 0; i < count; ++i) {
    g_string_append_printf(shown, " %s", bosonoga_value_kind_name(kinds[i]));
  }
  return g_string_free(shown, FALSE);
}

static gboolean bosonoga_kind_named(const gchar *text, gsize length,
                                    BosonogaValueKind *kind) {
  static const BosonogaValueKind kinds[] = {
      BOSONOGA_VALUE_I32,   BOSONOGA_VALUE_F32, BOSONOGA_VALUE_FLAGS,
      BOSONOGA_VALUE_COLOR, BOSONOGA_VALUE_STR,
  };
  for (gsize i = 0; i < G_N_ELEMENTS(kinds); ++i) {
    const gchar *name = bosonoga_value_kind_name(kinds[i]);
    if (strlen(name) == length && memcmp(name, text, length) == 0) {
      *kind = kinds[i];
      return TRUE;
    }
  }
  return FALSE;
}

gboolean bosonoga_parse_types(BosonogaParser *parser, BosonogaValueKind *kinds,
                              guint *count, const gchar *too_many,
                              GError **error) {
  do {
    const BosonogaToken *token = &parser->tokenizer.token;
    BosonogaValueKind kind = BOSONOGA_VALUE_I32;
    if (token->kind != BOSONOGA_TOKEN_WORD ||
        !bosonoga_kind_named(token->text, token->length, &kind)) {
      return bosonoga_parse_error(
          parser, "a type, which is i32, f32, flags, color or str", error);
    }
    if (*count == BOSONOGA_FUNCTION_VALUES_MAX) {
      return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_SYNTAX, "%s",
                              too_many);
    }
    kinds[(*count)++] = kind;
    if (!bosonoga_advance(parser, error)) {
      return FALSE;
    }
  } while (!bosonoga_at_word(parser, "di"));
  return TRUE;
}

// The type is after the last underscore, as in count_i32.
static gboolean bosonoga_parameter_kind(const BosonogaToken *token,
                                        BosonogaValueKind *kind) {
  if (token->kind != BOSONOGA_TOKEN_WORD) {
    return FALSE;
  }
  gsize underscore = token->length;
  while (underscore > 0 && token->text[underscore - 1] != '_') {
    --underscore;
  }
  return underscore > 1 &&
         bosonoga_kind_named(token->text + underscore,
                             token->length - underscore, kind);
}

static gboolean bosonoga_parse_function_rest(BosonogaParser *parser,
                                             BosonogaFunction *function,
                                             const BosonogaToken *name,
                                             GError **error) {
  while (!bosonoga_at_word(parser, "ret") && !bosonoga_at_word(parser, "di")) {
    BosonogaValueKind kind = BOSONOGA_VALUE_I32;
    if (!bosonoga_parameter_kind(&parser->tokenizer.token, &kind)) {
      return bosonoga_parse_error(
          parser, "a parameter whose name ends with its type, like count_i32",
          error);
    }
    if (function->parameter_count == BOSONOGA_FUNCTION_VALUES_MAX) {
      return bosonoga_fail_at(
          parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_SYNTAX,
          "a function takes at most %d values", BOSONOGA_FUNCTION_VALUES_MAX);
    }
    BosonogaToken parameter;
    if (!bosonoga_parse_new_name(parser, &parameter, error)) {
      return FALSE;
    }
    function->parameters[function->parameter_count++] = kind;
    bosonoga_declare_variable(parser, &parameter, kind, parser->slot_count++);
  }

  if (bosonoga_at_word(parser, "ret")) {
    g_autofree gchar *too_many = g_strdup_printf(
        "a function returns at most %d values", BOSONOGA_FUNCTION_VALUES_MAX);
    if (!bosonoga_advance(parser, error) ||
        !bosonoga_parse_types(parser, function->results,
                              &function->result_count, too_many, error)) {
      return FALSE;
    }
  }

  // It is defined before its block, so that the block can call it.
  function->entry = parser->ops->len;
  const guint index = parser->functions->len;
  g_array_append_val(parser->functions, *function);
  if (!bosonoga_parse_block(parser, error)) {
    return FALSE;
  }
  bosonoga_emit(parser,
                function->result_count > 0 ? BOSONOGA_OP_MISSING_RETURN
                                           : BOSONOGA_OP_FUNCTION_RETURN,
                name);
  g_array_index(parser->functions, BosonogaFunction, index).slot_count =
      parser->slot_count;
  return TRUE;
}

gboolean bosonoga_parse_function(BosonogaParser *parser, GError **error) {
  if (!bosonoga_expect_word(parser, "fn", error)) {
    return FALSE;
  }
  const BosonogaToken name = parser->tokenizer.token;
  if (!bosonoga_check_new_function_name(parser, &name, "a function name",
                                        error) ||
      !bosonoga_advance(parser, error)) {
    return FALSE;
  }

  BosonogaFunction function;
  memset(&function, 0, sizeof(function));
  function.name = name.text;
  function.name_length = name.length;

  // A function has its own variables and slots, sees no bosonoga_global,
  // and parses as bosonoga_main does, without the statements of gfx_frame.
  g_autoptr(GArray) variables =
      g_array_new(FALSE, FALSE, sizeof(BosonogaVariable));
  GArray *outer_variables = parser->variables;
  const guint outer_slot_count = parser->slot_count;
  const BosonogaSection outer_section = parser->section;
  parser->variables = variables;
  parser->slot_count = 0;
  parser->section = BOSONOGA_SECTION_MAIN;
  parser->in_function = TRUE;
  const gboolean parsed =
      bosonoga_parse_function_rest(parser, &function, &name, error);
  parser->in_function = FALSE;
  parser->section = outer_section;
  parser->slot_count = outer_slot_count;
  parser->variables = outer_variables;
  return parsed;
}

gboolean bosonoga_parse_call(BosonogaParser *parser, guint *function,
                             GError **error) {
  const BosonogaToken name = parser->tokenizer.token;
  const gint found = bosonoga_find_function(parser, &name);
  if (found < 0) {
    if (name.kind == BOSONOGA_TOKEN_WORD &&
        !bosonoga_token_is_reserved(parser, &name)) {
      g_autofree gchar *shown = bosonoga_token_shown(&name);
      return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_NAME,
                              "unknown function '%s'", shown);
    }
    return bosonoga_parse_error(parser, "a function name", error);
  }
  const BosonogaFunction called =
      g_array_index(parser->functions, BosonogaFunction, (guint)found);
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }

  g_autofree gchar *shown = bosonoga_token_shown(&name);
  if (called.is_array) {
    if (!bosonoga_at_value(parser)) {
      return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                              BOSONOGA_ERROR_TYPE,
                              "'%s' is an array, which is read at one i32 "
                              "index",
                              shown);
    }
    g_autofree gchar *context = g_strdup_printf("the index of '%s'", shown);
    BosonogaOperand index;
    if (!bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_I32, context,
                                      &index, error)) {
      return FALSE;
    }
    if (bosonoga_at_value(parser)) {
      return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                              BOSONOGA_ERROR_TYPE,
                              "'%s' is an array, which is read at one i32 "
                              "index, but is given more",
                              shown);
    }
    BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_ARRAY_READ, &name);
    op->function = (guint)found;
    op->left = index;
    *function = (guint)found;
    return TRUE;
  }
  g_autofree gchar *parameters =
      bosonoga_values_shown(called.parameters, called.parameter_count);
  for (guint i = 0; i < called.parameter_count; ++i) {
    const BosonogaToken value_token = parser->tokenizer.token;
    if (!bosonoga_at_value(parser)) {
      return bosonoga_fail_at(parser, &value_token, error, BOSONOGA_ERROR_TYPE,
                              "'%s' takes %s, but is given %u", shown,
                              parameters, i);
    }
    BosonogaOperand value;
    if (!bosonoga_parse_value_of_kind(parser, called.parameters[i], shown,
                                      &value, error)) {
      return FALSE;
    }
    BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_ARGUMENT, &value_token);
    op->index = i;
    op->left = value;
  }
  if (bosonoga_at_value(parser)) {
    return bosonoga_fail_at(
        parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_TYPE,
        "'%s' takes %s, but is given more", shown, parameters);
  }
  bosonoga_emit(parser, BOSONOGA_OP_CALL, &name)->function = (guint)found;
  *function = (guint)found;
  return TRUE;
}

static gchar *bosonoga_results_named(guint count) {
  if (count == 1) {
    return g_strdup(BOSONOGA_RESULT_PREFIX "0");
  }
  return g_strdup_printf(
      BOSONOGA_RESULT_PREFIX "0 to " BOSONOGA_RESULT_PREFIX "%u", count - 1);
}

static gboolean bosonoga_fail_needs_block(const BosonogaParser *parser,
                                          const BosonogaToken *token,
                                          const BosonogaFunction *called,
                                          GError **error) {
  g_autofree gchar *name = g_strndup(called->name, called->name_length);
  g_autofree gchar *results =
      bosonoga_values_shown(called->results, called->result_count);
  g_autofree gchar *named = bosonoga_results_named(called->result_count);
  return bosonoga_fail_at(
      parser, token, error, BOSONOGA_ERROR_TYPE,
      "'%s' %s %s, which %scall ... with ret takes in its block as %s", name,
      bosonoga_function_gives(called), results,
      called->result_count == 1 ? "var, set ... to or " : "", named);
}

gboolean bosonoga_check_single_result(const BosonogaParser *parser,
                                      const BosonogaToken *token, guint index,
                                      GError **error) {
  const BosonogaFunction *called =
      &g_array_index(parser->functions, BosonogaFunction, index);
  if (called->result_count == 1) {
    return TRUE;
  }
  if (called->result_count > 1) {
    return bosonoga_fail_needs_block(parser, token, called, error);
  }
  g_autofree gchar *name = g_strndup(called->name, called->name_length);
  return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                          "'%s' returns no values, so it is called with call",
                          name);
}

gboolean bosonoga_result_operand(const BosonogaParser *parser,
                                 const BosonogaToken *token,
                                 BosonogaOperand *operand, GError **error) {
  g_autofree gchar *shown = bosonoga_token_shown(token);
  if (parser->call_blocks->len == 0) {
    return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_SYNTAX,
                            "%s is a value of a call, so it can only be in "
                            "the block of a call",
                            shown);
  }
  const BosonogaCallBlock *block = &g_array_index(
      parser->call_blocks, BosonogaCallBlock, parser->call_blocks->len - 1);
  const BosonogaFunction *called =
      &g_array_index(parser->functions, BosonogaFunction, block->function);
  guint64 position = 0;
  if (!bosonoga_numbered_value(token, BOSONOGA_RESULT_PREFIX, G_MAXUINT32,
                               &position) ||
      position >= called->result_count) {
    g_autofree gchar *name = g_strndup(called->name, called->name_length);
    g_autofree gchar *named = bosonoga_results_named(called->result_count);
    return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_NAME,
                            "the call of '%s' has %s, not %s", name, named,
                            shown);
  }
  operand->literal.kind = called->results[position];
  operand->is_variable = TRUE;
  operand->slot = block->slot + (guint)position;
  return TRUE;
}

gboolean bosonoga_parse_call_statement(BosonogaParser *parser,
                                       const BosonogaToken *start,
                                       GError **error) {
  const BosonogaToken name = parser->tokenizer.token;
  guint index = 0;
  if (!bosonoga_parse_call(parser, &index, error)) {
    return FALSE;
  }
  const BosonogaFunction *called =
      &g_array_index(parser->functions, BosonogaFunction, index);
  const guint count = called->result_count;
  if (count == 0) {
    if (bosonoga_at_word(parser, "with") || bosonoga_at_word(parser, "di")) {
      g_autofree gchar *shown = bosonoga_token_shown(&name);
      return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                              BOSONOGA_ERROR_SYNTAX,
                              "'%s' returns no values, so its call has neither "
                              "with ret nor a block",
                              shown);
    }
    return TRUE;
  }
  if (!bosonoga_at_word(parser, "with")) {
    return bosonoga_fail_needs_block(parser, start, called, error);
  }
  if (!bosonoga_advance(parser, error) ||
      !bosonoga_expect_word(parser, "ret", error)) {
    return FALSE;
  }

  // Copied out first, as calls in the block replace the results.
  const BosonogaCallBlock block = {index, parser->slot_count};
  parser->slot_count += count;
  for (guint i = 0; i < count; ++i) {
    BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_TAKE_RESULT, &name);
    op->slot = block.slot + i;
    op->index = i;
  }
  g_array_append_val(parser->call_blocks, block);
  if (!bosonoga_parse_block(parser, error)) {
    return FALSE;
  }
  g_array_set_size(parser->call_blocks, parser->call_blocks->len - 1);
  return TRUE;
}

gboolean bosonoga_parse_function_return(BosonogaParser *parser,
                                        const BosonogaToken *start,
                                        GError **error) {
  const BosonogaFunction *function = &g_array_index(
      parser->functions, BosonogaFunction, parser->functions->len - 1);
  if (bosonoga_at_word(parser, "exit_success") ||
      bosonoga_at_word(parser, "exit_failure")) {
    return bosonoga_fail_at(
        parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_SYNTAX,
        "only bosonoga_main and gfx_main return an exit status, a "
        "function returns its values");
  }
  g_autofree gchar *name = g_strndup(function->name, function->name_length);
  g_autofree gchar *results =
      bosonoga_values_shown(function->results, function->result_count);
  for (guint i = 0; i < function->result_count; ++i) {
    const BosonogaToken value_token = parser->tokenizer.token;
    if (!bosonoga_at_value(parser)) {
      return bosonoga_fail_at(parser, start, error, BOSONOGA_ERROR_TYPE,
                              "'%s' returns %s, but this return has %u", name,
                              results, i);
    }
    BosonogaOperand value;
    if (!bosonoga_parse_value_of_kind(parser, function->results[i], "return",
                                      &value, error)) {
      return FALSE;
    }
    BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_RESULT, &value_token);
    op->index = i;
    op->left = value;
  }
  if (bosonoga_at_value(parser)) {
    return bosonoga_fail_at(parser, start, error, BOSONOGA_ERROR_TYPE,
                            "'%s' returns %s, but this return has more", name,
                            results);
  }
  bosonoga_emit(parser, BOSONOGA_OP_FUNCTION_RETURN, start);
  return TRUE;
}
