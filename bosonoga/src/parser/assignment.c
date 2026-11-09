#include <string.h>

#include "parser/internal.h"

static const struct {
  const gchar *word;
  BosonogaOpKind op;
  BosonogaValueKind source;
  BosonogaValueKind result;
  gboolean is_binary;
} bosonoga_conversions[] = {
    {"to_f32", BOSONOGA_OP_CONVERT_TO_F32, BOSONOGA_VALUE_I32,
     BOSONOGA_VALUE_F32, FALSE},
    {"to_i32", BOSONOGA_OP_CONVERT_TO_I32, BOSONOGA_VALUE_F32,
     BOSONOGA_VALUE_I32, FALSE},
    {"to_sin", BOSONOGA_OP_SIN, BOSONOGA_VALUE_F32, BOSONOGA_VALUE_F32, FALSE},
    {"to_cos", BOSONOGA_OP_COS, BOSONOGA_VALUE_F32, BOSONOGA_VALUE_F32, FALSE},
    {"to_atan2", BOSONOGA_OP_ATAN2, BOSONOGA_VALUE_F32, BOSONOGA_VALUE_F32,
     TRUE},
    {"to_sqrt", BOSONOGA_OP_SQRT, BOSONOGA_VALUE_F32, BOSONOGA_VALUE_F32,
     FALSE},
    {"to_pow", BOSONOGA_OP_POW, BOSONOGA_VALUE_F32, BOSONOGA_VALUE_F32, TRUE},
};

static gint bosonoga_find_conversion(const BosonogaParser *parser) {
  for (gsize i = 0; i < G_N_ELEMENTS(bosonoga_conversions); ++i) {
    if (bosonoga_at_word(parser, bosonoga_conversions[i].word)) {
      return (gint)i;
    }
  }
  return -1;
}

static gboolean bosonoga_at_conversion(const BosonogaParser *parser) {
  return bosonoga_find_conversion(parser) >= 0;
}

static gboolean bosonoga_parse_conversion(BosonogaParser *parser, guint slot,
                                          BosonogaValueKind *kind,
                                          GError **error) {
  const BosonogaToken start = parser->tokenizer.token;
  const gint found = bosonoga_find_conversion(parser);
  g_return_val_if_fail(found >= 0, FALSE);
  const gsize index = (gsize)found;
  const gchar *word = bosonoga_conversions[index].word;
  const BosonogaValueKind source_kind = bosonoga_conversions[index].source;
  BosonogaOperand left;
  BosonogaOperand right;
  memset(&right, 0, sizeof(right));
  if (!bosonoga_advance(parser, error) ||
      !bosonoga_parse_value_of_kind(parser, source_kind, word, &left, error) ||
      (bosonoga_conversions[index].is_binary &&
       !bosonoga_parse_value_of_kind(parser, source_kind, word, &right,
                                     error))) {
    return FALSE;
  }
  BosonogaOp *op =
      bosonoga_emit(parser, bosonoga_conversions[index].op, &start);
  op->slot = slot;
  op->left = left;
  op->right = right;
  *kind = bosonoga_conversions[index].result;
  return TRUE;
}

static gboolean bosonoga_at_name(const BosonogaParser *parser) {
  const BosonogaToken *token = &parser->tokenizer.token;
  return token->kind == BOSONOGA_TOKEN_WORD &&
         !bosonoga_token_is_reserved(parser, token);
}

gboolean bosonoga_parse_var(BosonogaParser *parser, GError **error) {
  BosonogaToken name;
  if (!bosonoga_parse_new_name(parser, &name, error)) {
    return FALSE;
  }
  if (bosonoga_at_name(parser)) {
    return bosonoga_fail_at(
        parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_SYNTAX,
        "a var declares one variable, and the values of a "
        "call are taken with call ... with ret, one per line");
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
    bosonoga_declare_variable(parser, &name, called->results[0], slot);
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

  bosonoga_declare_variable(parser, &name, kind, slot);
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
    identity.literal.kind = kind;
    if (step == BOSONOGA_OP_ACCUMULATOR_DIVIDE) {
      if (kind == BOSONOGA_VALUE_F32) {
        identity.literal.f32 = 1.0f;
      } else {
        identity.literal.i32 = 1;
      }
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

// A str can only be set with to_add and a single str.
static gboolean bosonoga_parse_set_str(BosonogaParser *parser,
                                       const BosonogaVariable *target,
                                       const BosonogaToken *operation,
                                       const gchar *shown, GError **error) {
  if (!bosonoga_at_word(parser, "to_add")) {
    return bosonoga_fail_at(parser, operation, error, BOSONOGA_ERROR_TYPE,
                            "'%s' is str, which cannot be used in arithmetic, "
                            "but to_add with a single str sets it",
                            shown);
  }
  BosonogaOperand value;
  if (!bosonoga_advance(parser, error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_STR, "to_add",
                                    &value, error)) {
    return FALSE;
  }
  if (bosonoga_at_value(parser)) {
    return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                            BOSONOGA_ERROR_TYPE,
                            "strings cannot be joined, to_add sets '%s' to a "
                            "single str",
                            shown);
  }
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_ASSIGN, operation);
  op->slot = target->slot;
  op->left = value;
  return TRUE;
}

static gboolean bosonoga_parse_set_to(BosonogaParser *parser,
                                      const BosonogaToken *name,
                                      const BosonogaVariable *target,
                                      GError **error) {
  guint index = 0;
  if (!bosonoga_expect_word(parser, "to", error) ||
      !bosonoga_parse_call(parser, &index, error) ||
      !bosonoga_check_single_result(parser, name, index, error) ||
      !bosonoga_parse_taken_result(parser, index, error)) {
    return FALSE;
  }
  const BosonogaFunction *called =
      &g_array_index(parser->functions, BosonogaFunction, index);
  if (target->kind != called->results[0]) {
    g_autofree gchar *shown = bosonoga_token_shown(name);
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
  const BosonogaVariable *found = bosonoga_find_variable(parser, &name);
  if (found == NULL) {
    return bosonoga_fail_unknown_variable(parser, &name, error);
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
    return bosonoga_parse_set_to(parser, &name, &target, error);
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
                                "to_add, to_subtract, to_multiply, to_divide, "
                                "to_mod or a conversion",
                                error);
  }
  if (target.kind == BOSONOGA_VALUE_STR) {
    return bosonoga_parse_set_str(parser, &target, &operation, shown, error);
  }
  if (target.kind == BOSONOGA_VALUE_FLAGS ||
      target.kind == BOSONOGA_VALUE_COLOR) {
    return bosonoga_fail_at(parser, &operation, error, BOSONOGA_ERROR_TYPE,
                            "'%s' is %s, which cannot be used in arithmetic",
                            shown, bosonoga_value_kind_name(target.kind));
  }
  return bosonoga_advance(parser, error) &&
         bosonoga_parse_accumulation(parser, &target, &name,
                                     accumulations[i].word,
                                     accumulations[i].step, error);
}
