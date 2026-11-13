#include <string.h>

#include "parser/internal.h"

gboolean bosonoga_parse_call(BosonogaParser *parser, guint *function,
                             GError **error) {
  const BosonogaToken name = parser->tokenizer.token;
  const gint found = bosonoga_find_function(parser, &name);
  if (found < 0) {
    if (!bosonoga_check_not_hidden(parser, &name, error)) {
      return FALSE;
    }
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
  bosonoga_use_function(parser, &name, (guint)found);
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

static gchar *bosonoga_results_named(const BosonogaFunction *called) {
  GString *named = g_string_new(NULL);
  for (guint i = 0; i < called->result_count; ++i) {
    g_string_append_printf(named, "%s%u_%s", i == 0 ? "" : ", ", i,
                           bosonoga_value_kind_name(called->results[i]));
  }
  return g_string_free(named, FALSE);
}

static gboolean bosonoga_fail_needs_block(const BosonogaParser *parser,
                                          const BosonogaToken *token,
                                          const BosonogaFunction *called,
                                          GError **error) {
  g_autofree gchar *name = g_strndup(called->name, called->name_length);
  g_autofree gchar *named = bosonoga_results_named(called);
  return bosonoga_fail_at(
      parser, token, error, BOSONOGA_ERROR_TYPE,
      "'%s' %s %s, which %scall ... with ret takes for its block, each "
      "named, as in with ret 0_%s as value",
      name, bosonoga_function_gives(called), named,
      called->result_count == 1 ? "var, set ... to or " : "",
      bosonoga_value_kind_name(called->results[0]));
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

static gboolean bosonoga_parse_bindings(BosonogaParser *parser,
                                        const BosonogaToken *call_name,
                                        guint index, GError **error) {
  const BosonogaFunction called =
      g_array_index(parser->functions, BosonogaFunction, index);
  g_autofree gchar *name = g_strndup(called.name, called.name_length);
  gboolean has_previous = FALSE;
  guint64 previous = 0;
  BosonogaValueKind previous_kind = BOSONOGA_VALUE_I32;
  do {
    const BosonogaToken position_token = parser->tokenizer.token;
    guint64 position = 0;
    BosonogaValueKind kind = BOSONOGA_VALUE_I32;
    if (!bosonoga_token_indexed(&position_token, &position, &kind)) {
      if (has_previous) {
        return bosonoga_parse_error(
            parser, "another value to take, like 1_i32 as count, or di", error);
      }
      return bosonoga_fail_needs_block(parser, &position_token, &called, error);
    }
    g_autofree gchar *shown = bosonoga_token_shown(&position_token);
    if (position >= called.result_count || kind != called.results[position]) {
      g_autofree gchar *named = bosonoga_results_named(&called);
      return bosonoga_fail_at(parser, &position_token, error,
                              BOSONOGA_ERROR_TYPE,
                              "'%s' %s %s, so it has no %s", name,
                              bosonoga_function_gives(&called), named, shown);
    }
    if (has_previous && position <= previous) {
      return bosonoga_fail_at(
          parser, &position_token, error, BOSONOGA_ERROR_SYNTAX,
          "the values are taken once each, by increasing "
          "index, so %s cannot follow %" G_GUINT64_FORMAT "_%s",
          shown, previous, bosonoga_value_kind_name(previous_kind));
    }
    if (!bosonoga_advance(parser, error)) {
      return FALSE;
    }
    if (!bosonoga_at_word(parser, "as")) {
      g_autofree gchar *got = bosonoga_token_shown(&parser->tokenizer.token);
      return bosonoga_fail_at(
          parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_SYNTAX,
          "expected as and a name after %s, as in %s as "
          "value, got '%s'",
          shown, shown,
          parser->tokenizer.token.kind == BOSONOGA_TOKEN_END ? "end of file"
                                                             : got);
    }
    BosonogaToken binding;
    if (!bosonoga_advance(parser, error) ||
        !bosonoga_parse_new_name(parser, &binding, error)) {
      return FALSE;
    }
    const guint slot = parser->slot_count++;
    BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_TAKE_RESULT, call_name);
    op->slot = slot;
    op->index = (guint)position;
    bosonoga_declare_variable(parser, &binding, kind, slot);
    has_previous = TRUE;
    previous = position;
    previous_kind = kind;
  } while (!bosonoga_at_word(parser, "di"));
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
  const guint visible_count = parser->variables->len;
  if (!bosonoga_parse_bindings(parser, &name, index, error) ||
      !bosonoga_parse_block(parser, error) ||
      !bosonoga_check_variables_used(parser, visible_count, error)) {
    return FALSE;
  }
  g_array_set_size(parser->variables, visible_count);
  return TRUE;
}

gboolean bosonoga_parse_function_return(BosonogaParser *parser,
                                        const BosonogaToken *start,
                                        GError **error) {
  const BosonogaFunction *function =
      &g_array_index(parser->functions, BosonogaFunction, parser->function);
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
