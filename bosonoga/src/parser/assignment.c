#include <string.h>

#include "parser/internal.h"

static gboolean bosonoga_at_name(const BosonogaParser *parser) {
  const BosonogaToken *token = &parser->tokenizer.token;
  return token->kind == BOSONOGA_TOKEN_WORD &&
         !bosonoga_token_is_reserved(parser, token);
}

static void bosonoga_declare(BosonogaParser *parser, const BosonogaToken *name,
                             BosonogaValueKind kind, guint slot,
                             gboolean is_val) {
  bosonoga_declare_variable(parser, name, kind, slot);
  g_array_index(parser->variables, BosonogaVariable, parser->variables->len - 1)
      .is_val = is_val;
}

gboolean bosonoga_parse_var(BosonogaParser *parser, gboolean is_val,
                            GError **error) {
  BosonogaToken name;
  if (!bosonoga_parse_new_name(parser, &name, error)) {
    return FALSE;
  }
  if (bosonoga_at_name(parser)) {
    return bosonoga_fail_at(
        parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_SYNTAX,
        "a %s declares one variable, and the values of a "
        "call are taken with call ... with ret, one per line",
        is_val ? "val" : "var");
  }
  if (!bosonoga_expect_word(parser, "is", error)) {
    return FALSE;
  }

  // Declared after the call, so that its arguments cannot refer to it.
  if (bosonoga_find_function(parser, &parser->tokenizer.token) >= 0) {
    guint index = 0;
    if (!bosonoga_parse_call(parser, &index, error) ||
        !bosonoga_check_single_result(parser, &name, index, error) ||
        !bosonoga_parse_taken_result(parser, index, error)) {
      return FALSE;
    }
    const BosonogaFunction *called =
        &g_array_index(parser->functions, BosonogaFunction, index);
    const guint slot = parser->slot_count++;
    bosonoga_declare(parser, &name, called->results[0], slot, is_val);
    BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_TAKE_RESULT, &name);
    op->slot = slot;
    op->index = 0;
    return TRUE;
  }

  const guint slot = parser->slot_count++;
  BosonogaValueKind kind = BOSONOGA_VALUE_I32;
  if (bosonoga_at_conversion(parser)) {
    if (!bosonoga_parse_conversion(parser, slot, &kind, error)) {
      return FALSE;
    }
  } else {
    const BosonogaToken start = parser->tokenizer.token;
    BosonogaOperand initial;
    if (!bosonoga_parse_value(parser, &initial, error)) {
      return FALSE;
    }
    kind = initial.literal.kind;
    BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_ASSIGN, &start);
    op->slot = slot;
    op->left = initial;

    while (kind == BOSONOGA_VALUE_FLAGS &&
           bosonoga_at_word(parser, "shift_or")) {
      const BosonogaToken shift_or = parser->tokenizer.token;
      BosonogaOperand flags;
      if (!bosonoga_advance(parser, error) ||
          !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_FLAGS,
                                        "shift_or", &flags, error)) {
        return FALSE;
      }
      op = bosonoga_emit(parser, BOSONOGA_OP_FLAGS_OR, &shift_or);
      op->slot = slot;
      op->left = flags;
    }
  }

  bosonoga_declare(parser, &name, kind, slot, is_val);
  return TRUE;
}

static gboolean bosonoga_parse_accumulation(BosonogaParser *parser,
                                            const BosonogaVariable *target,
                                            const BosonogaToken *start,
                                            const gchar *context,
                                            BosonogaOpKind step,
                                            GError **error) {
  const gboolean has_inverse = step == BOSONOGA_OP_ACCUMULATOR_SUBTRACT ||
                               step == BOSONOGA_OP_ACCUMULATOR_DIVIDE;
  const BosonogaValueKind kind = target->kind;
  const guint slot = target->slot;

  BosonogaOperand first;
  if (!bosonoga_parse_value_of_kind(parser, kind, context, &first, error)) {
    return FALSE;
  }
  if (step == BOSONOGA_OP_ACCUMULATOR_MOD && !bosonoga_at_value(parser)) {
    return bosonoga_parse_error(parser, "a value to divide by", error);
  }
  if (has_inverse && !bosonoga_at_value(parser)) {
    BosonogaOperand identity;
    memset(&identity, 0, sizeof(identity));
    const gint one = step == BOSONOGA_OP_ACCUMULATOR_DIVIDE;
    if (bosonoga_kind_is_integer(kind)) {
      bosonoga_set_whole(&identity.literal, kind, one);
    } else {
      bosonoga_set_real(&identity.literal, kind, one);
    }
    bosonoga_emit(parser, BOSONOGA_OP_ACCUMULATOR_LOAD, start)->left = identity;
    bosonoga_emit(parser, step, start)->left = first;
  } else {
    bosonoga_emit(parser, BOSONOGA_OP_ACCUMULATOR_LOAD, start)->left = first;
  }

  while (bosonoga_at_value(parser)) {
    BosonogaOperand next;
    if (!bosonoga_parse_value_of_kind(parser, kind, context, &next, error)) {
      return FALSE;
    }
    bosonoga_emit(parser, step, start)->left = next;
  }
  bosonoga_emit(parser, BOSONOGA_OP_ACCUMULATOR_STORE, start)->slot = slot;
  return TRUE;
}

static gboolean bosonoga_parse_set_to(BosonogaParser *parser,
                                      const BosonogaToken *name,
                                      const BosonogaVariable *target,
                                      const gchar *shown, GError **error) {
  if (!bosonoga_expect_word(parser, "to", error)) {
    return FALSE;
  }
  if (bosonoga_find_function(parser, &parser->tokenizer.token) < 0) {
    BosonogaOperand value;
    if (!bosonoga_parse_value_of_kind(parser, target->kind, "set ... to",
                                      &value, error)) {
      return FALSE;
    }
    if (bosonoga_at_value(parser)) {
      return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                              BOSONOGA_ERROR_SYNTAX,
                              "set ... to sets '%s' to a single value", shown);
    }
    BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_ASSIGN, name);
    op->slot = target->slot;
    op->left = value;
    return TRUE;
  }

  guint index = 0;
  if (!bosonoga_parse_call(parser, &index, error) ||
      !bosonoga_check_single_result(parser, name, index, error) ||
      !bosonoga_parse_taken_result(parser, index, error)) {
    return FALSE;
  }
  const BosonogaFunction *called =
      &g_array_index(parser->functions, BosonogaFunction, index);
  if (target->kind != called->results[0]) {
    g_autofree gchar *function = g_strndup(called->name, called->name_length);
    return bosonoga_fail_at(parser, name, error, BOSONOGA_ERROR_TYPE,
                            "'%s' is %s, but '%s' %s %s", shown,
                            bosonoga_value_kind_name(target->kind), function,
                            bosonoga_function_gives(called),
                            bosonoga_value_kind_name(called->results[0]));
  }
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_TAKE_RESULT, name);
  op->slot = target->slot;
  op->index = 0;
  return TRUE;
}

gboolean bosonoga_parse_set(BosonogaParser *parser, GError **error) {
  const BosonogaToken name = parser->tokenizer.token;
  if (name.kind != BOSONOGA_TOKEN_WORD ||
      bosonoga_token_is_reserved(parser, &name)) {
    return bosonoga_parse_error(parser, "a variable name", error);
  }
  g_autofree gchar *shown = bosonoga_token_shown(&name);
  if (bosonoga_find_constant(parser, &name) != NULL) {
    return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_TYPE,
                            "'%s' is a constant and cannot be set", shown);
  }
  const BosonogaVariable *found = bosonoga_find_variable(parser, &name);
  if (found == NULL) {
    return bosonoga_fail_unknown_variable(parser, &name, error);
  }
  if (found->is_val) {
    return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_TYPE,
                            "'%s' is a val and cannot be set", shown);
  }
  const BosonogaVariable target = *found;
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  if (bosonoga_at_name(parser)) {
    return bosonoga_fail_at(
        parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_SYNTAX,
        "a set sets one variable, and the values of a "
        "call are taken with call ... with ret, one per line");
  }
  if (bosonoga_at_word(parser, "to")) {
    return bosonoga_parse_set_to(parser, &name, &target, shown, error);
  }

  const BosonogaToken operation = parser->tokenizer.token;
  if (bosonoga_at_conversion(parser)) {
    BosonogaValueKind kind = BOSONOGA_VALUE_I32;
    if (!bosonoga_parse_conversion(parser, target.slot, &kind, error)) {
      return FALSE;
    }
    if (kind != target.kind) {
      return bosonoga_fail_at(parser, &operation, error, BOSONOGA_ERROR_TYPE,
                              "'%s' is %s, but the conversion gives %s", shown,
                              bosonoga_value_kind_name(target.kind),
                              bosonoga_value_kind_name(kind));
    }
    return TRUE;
  }

  static const struct {
    const gchar *word;
    BosonogaOpKind step;
  } accumulations[] = {
      {"to_add", BOSONOGA_OP_ACCUMULATOR_ADD},
      {"to_subtract", BOSONOGA_OP_ACCUMULATOR_SUBTRACT},
      {"to_multiply", BOSONOGA_OP_ACCUMULATOR_MULTIPLY},
      {"to_divide", BOSONOGA_OP_ACCUMULATOR_DIVIDE},
      {"to_mod", BOSONOGA_OP_ACCUMULATOR_MOD},
  };
  gsize i = 0;
  while (i < G_N_ELEMENTS(accumulations) &&
         !bosonoga_at_word(parser, accumulations[i].word)) {
    ++i;
  }
  if (i == G_N_ELEMENTS(accumulations)) {
    return bosonoga_parse_error(parser,
                                "to, to_add, to_subtract, to_multiply, "
                                "to_divide, to_mod or a conversion",
                                error);
  }
  if (!bosonoga_kind_is_number(target.kind)) {
    return bosonoga_fail_at(parser, &operation, error, BOSONOGA_ERROR_TYPE,
                            "'%s' is %s, which cannot be used in arithmetic, "
                            "but set ... to sets it",
                            shown, bosonoga_value_kind_name(target.kind));
  }
  return bosonoga_advance(parser, error) &&
         bosonoga_parse_accumulation(parser, &target, &name,
                                     accumulations[i].word,
                                     accumulations[i].step, error);
}
