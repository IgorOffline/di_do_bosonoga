#include <string.h>

#include "parser/internal.h"

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
    return bosonoga_parse_error(
        parser, "a function name, which ends with " BOSONOGA_FUNCTION_SUFFIX,
        error);
  }
  const BosonogaFunction called =
      g_array_index(parser->functions, BosonogaFunction, (guint)found);
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }

  g_autofree gchar *shown = bosonoga_token_shown(&name);
  if (called.is_array) {
    *function = (guint)found;
    return bosonoga_parse_array_read(parser, &name, (guint)found, error);
  }
  if (bosonoga_at_word(parser, "at")) {
    return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                            BOSONOGA_ERROR_SYNTAX,
                            "'%s' is a function, and only an array is read "
                            "at an index",
                            shown);
  }
  g_autofree gchar *parameters =
      bosonoga_values_shown(called.parameters, called.parameter_count);
  if (called.parameter_count == 0 && bosonoga_at_word(parser, "of")) {
    return bosonoga_fail_at(
        parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_SYNTAX,
        "'%s' takes no values, so its call has no of", shown);
  }
  if (called.parameter_count > 0) {
    if (!bosonoga_at_word(parser, "of")) {
      return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                              BOSONOGA_ERROR_SYNTAX,
                              "'%s' takes %s, which follow of, as in %s of "
                              "...",
                              shown, parameters, shown);
    }
    if (!bosonoga_advance(parser, error)) {
      return FALSE;
    }
  }
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

// As in "ret_0_i32, ret_1_str".
static gchar *bosonoga_results_named(const BosonogaFunction *called) {
  GString *named = g_string_new(NULL);
  for (guint i = 0; i < called->result_count; ++i) {
    g_string_append_printf(named, "%s" BOSONOGA_RESULT_PREFIX "%u_%s",
                           i == 0 ? "" : ", ", i,
                           bosonoga_value_kind_name(called->results[i]));
  }
  return g_string_free(named, FALSE);
}

static gboolean bosonoga_fail_needs_block(const BosonogaParser *parser,
                                          const BosonogaToken *token,
                                          const BosonogaFunction *called,
                                          GError **error) {
  g_autofree gchar *name = g_strndup(called->name, called->name_length);
  g_autofree gchar *results =
      bosonoga_values_shown(called->results, called->result_count);
  g_autofree gchar *named = bosonoga_results_named(called);
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
  const gsize prefix_length = strlen(BOSONOGA_RESULT_PREFIX);
  guint64 position = 0;
  BosonogaValueKind kind = BOSONOGA_VALUE_I32;
  if (!bosonoga_split_indexed(token->text + prefix_length,
                              token->length - prefix_length, &position,
                              &kind) ||
      position >= called->result_count || kind != called->results[position]) {
    g_autofree gchar *name = g_strndup(called->name, called->name_length);
    g_autofree gchar *named = bosonoga_results_named(called);
    return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_NAME,
                            "the call of '%s' has %s, not %s", name, named,
                            shown);
  }
  operand->literal.kind = kind;
  operand->is_variable = TRUE;
  operand->slot = block->slot + (guint)position;
  return TRUE;
}

gboolean bosonoga_parse_taken_result(BosonogaParser *parser, guint index,
                                     GError **error) {
  const BosonogaFunction *called =
      &g_array_index(parser->functions, BosonogaFunction, index);
  g_autofree gchar *name = g_strndup(called->name, called->name_length);
  const gchar *type = bosonoga_value_kind_name(called->results[0]);
  if (!bosonoga_at_word(parser, "with")) {
    g_autofree gchar *shown = bosonoga_token_shown(&parser->tokenizer.token);
    return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                            BOSONOGA_ERROR_SYNTAX,
                            "expected with ret 0_%s, the value of '%s' that "
                            "is taken, got '%s'",
                            type, name, shown);
  }
  if (!bosonoga_advance(parser, error) ||
      !bosonoga_expect_word(parser, "ret", error)) {
    return FALSE;
  }
  const BosonogaToken *token = &parser->tokenizer.token;
  guint64 position = 0;
  BosonogaValueKind kind = BOSONOGA_VALUE_I32;
  if (!bosonoga_token_indexed(token, &position, &kind) || position != 0 ||
      kind != called->results[0]) {
    g_autofree gchar *shown = bosonoga_token_shown(token);
    return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                            "'%s' %s %s, so its call ends with ret 0_%s, not "
                            "'%s'",
                            name, bosonoga_function_gives(called), type, type,
                            shown);
  }
  return bosonoga_advance(parser, error);
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
