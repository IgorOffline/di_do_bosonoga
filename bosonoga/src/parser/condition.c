#include "parser/internal.h"

static gboolean bosonoga_parse_compare_kind(const BosonogaParser *parser,
                                            BosonogaCompareKind *compare,
                                            GError **error) {
  static const struct {
    const gchar *word;
    BosonogaCompareKind kind;
  } compares[] = {
      {"gt", BOSONOGA_COMPARE_GT},
      {"lt", BOSONOGA_COMPARE_LT},
      {"ge", BOSONOGA_COMPARE_GE},
      {"le", BOSONOGA_COMPARE_LE},
      {"eq", BOSONOGA_COMPARE_EQ},
      {"not_eq", BOSONOGA_COMPARE_NOT_EQ},
      {"eq_3", BOSONOGA_COMPARE_EQ_3},
      {"eq_6", BOSONOGA_COMPARE_EQ_6},
      {"not_eq_3", BOSONOGA_COMPARE_NOT_EQ_3},
      {"not_eq_6", BOSONOGA_COMPARE_NOT_EQ_6},
      {"shift_has", BOSONOGA_COMPARE_SHIFT_HAS},
  };
  for (gsize i = 0; i < G_N_ELEMENTS(compares); ++i) {
    if (bosonoga_at_word(parser, compares[i].word)) {
      *compare = compares[i].kind;
      return TRUE;
    }
  }
  return bosonoga_parse_error(parser, "a comparison", error);
}

static gboolean bosonoga_check_compare(const BosonogaParser *parser,
                                       const BosonogaToken *token,
                                       BosonogaCompareKind compare,
                                       BosonogaValueKind left,
                                       BosonogaValueKind right,
                                       GError **error) {
  g_autofree gchar *shown = bosonoga_token_shown(token);
  if (compare == BOSONOGA_COMPARE_SHIFT_HAS) {
    if (left == BOSONOGA_VALUE_FLAGS && right == BOSONOGA_VALUE_FLAGS) {
      return TRUE;
    }
    return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                            "shift_has needs flags, not %s and %s",
                            bosonoga_value_kind_name(left),
                            bosonoga_value_kind_name(right));
  }
  if (left != right) {
    return bosonoga_fail_at(
        parser, token, error, BOSONOGA_ERROR_TYPE, "cannot compare %s with %s",
        bosonoga_value_kind_name(left), bosonoga_value_kind_name(right));
  }
  if (left == BOSONOGA_VALUE_FLAGS) {
    return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                            "flags can only be compared with shift_has");
  }
  if (left == BOSONOGA_VALUE_COLOR) {
    return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                            "colors cannot be compared");
  }
  if (left == BOSONOGA_VALUE_STR && compare != BOSONOGA_COMPARE_EQ &&
      compare != BOSONOGA_COMPARE_NOT_EQ) {
    return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                            "str can only be compared with eq or not_eq");
  }

  switch (compare) {
    case BOSONOGA_COMPARE_GT:
    case BOSONOGA_COMPARE_LT:
    case BOSONOGA_COMPARE_GE:
    case BOSONOGA_COMPARE_LE:
      return TRUE;
    case BOSONOGA_COMPARE_EQ:
    case BOSONOGA_COMPARE_NOT_EQ:
      if (left == BOSONOGA_VALUE_F32) {
        return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                                "f32 has no exact %s, use %s_3 or %s_6", shown,
                                shown, shown);
      }
      return TRUE;
    case BOSONOGA_COMPARE_EQ_3:
    case BOSONOGA_COMPARE_EQ_6:
    case BOSONOGA_COMPARE_NOT_EQ_3:
    case BOSONOGA_COMPARE_NOT_EQ_6:
      if (left == BOSONOGA_VALUE_I32) {
        return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                                "%s only works on f32, i32 uses eq or not_eq",
                                shown);
      }
      return TRUE;
    case BOSONOGA_COMPARE_SHIFT_HAS:
      break;
  }
  g_return_val_if_reached(FALSE);
}

static gboolean bosonoga_parse_comparison(BosonogaParser *parser,
                                          GArray *false_jumps, GError **error) {
  BosonogaOperand left;
  BosonogaOperand right;
  BosonogaCompareKind compare = BOSONOGA_COMPARE_EQ;
  const gboolean negate = bosonoga_at_word(parser, "not");
  if (negate && !bosonoga_advance(parser, error)) {
    return FALSE;
  }
  if (!bosonoga_parse_value(parser, &left, error) ||
      !bosonoga_parse_compare_kind(parser, &compare, error)) {
    return FALSE;
  }
  const BosonogaToken operation = parser->tokenizer.token;
  if (!bosonoga_advance(parser, error) ||
      !bosonoga_parse_value(parser, &right, error) ||
      !bosonoga_check_compare(parser, &operation, compare, left.literal.kind,
                              right.literal.kind, error)) {
    return FALSE;
  }

  const guint jump = parser->ops->len;
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_JUMP_UNLESS, &operation);
  op->compare = compare;
  op->negate = negate;
  op->left = left;
  op->right = right;
  g_array_append_val(false_jumps, jump);
  return TRUE;
}

// Each group of "and" jumps to the next group as soon as a comparison is false,
// and to the true case when it reaches its end, so that "and" binds tighter.
gboolean bosonoga_parse_condition(BosonogaParser *parser, GArray *false_jumps,
                                  GError **error) {
  g_autoptr(GArray) true_jumps = g_array_new(FALSE, FALSE, sizeof(guint));
  while (TRUE) {
    if (!bosonoga_parse_comparison(parser, false_jumps, error)) {
      return FALSE;
    }
    if (bosonoga_at_word(parser, "and")) {
      if (!bosonoga_advance(parser, error)) {
        return FALSE;
      }
    } else if (bosonoga_at_word(parser, "or")) {
      const guint jump = parser->ops->len;
      bosonoga_emit(parser, BOSONOGA_OP_JUMP, &parser->tokenizer.token);
      g_array_append_val(true_jumps, jump);
      bosonoga_patch_jumps_to_here(parser, false_jumps);
      g_array_set_size(false_jumps, 0);
      if (!bosonoga_advance(parser, error)) {
        return FALSE;
      }
    } else {
      bosonoga_patch_jumps_to_here(parser, true_jumps);
      return TRUE;
    }
  }
}
