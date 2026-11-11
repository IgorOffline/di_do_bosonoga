#include <string.h>

#include "parser/internal.h"

static const struct {
  const gchar *word;
  BosonogaCompareKind kind;
} bosonoga_compares[] = {
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

static gboolean bosonoga_at_compare(const BosonogaParser *parser,
                                    BosonogaCompareKind *compare) {
  for (gsize i = 0; i < G_N_ELEMENTS(bosonoga_compares); ++i) {
    if (bosonoga_at_word(parser, bosonoga_compares[i].word)) {
      *compare = bosonoga_compares[i].kind;
      return TRUE;
    }
  }
  return FALSE;
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
  if ((left == BOSONOGA_VALUE_STR || left == BOSONOGA_VALUE_BOOL ||
       left == BOSONOGA_VALUE_INPUT_KEY) &&
      compare != BOSONOGA_COMPARE_EQ && compare != BOSONOGA_COMPARE_NOT_EQ) {
    return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                            "%s can only be compared with eq or not_eq",
                            bosonoga_value_kind_name(left));
  }

  switch (compare) {
    case BOSONOGA_COMPARE_GT:
    case BOSONOGA_COMPARE_LT:
    case BOSONOGA_COMPARE_GE:
    case BOSONOGA_COMPARE_LE:
      return TRUE;
    case BOSONOGA_COMPARE_EQ:
    case BOSONOGA_COMPARE_NOT_EQ:
      if (bosonoga_kind_is_real(left)) {
        return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                                "%s has no exact %s, use %s_3 or %s_6",
                                bosonoga_value_kind_name(left), shown, shown,
                                shown);
      }
      return TRUE;
    case BOSONOGA_COMPARE_EQ_3:
    case BOSONOGA_COMPARE_EQ_6:
    case BOSONOGA_COMPARE_NOT_EQ_3:
    case BOSONOGA_COMPARE_NOT_EQ_6:
      if (bosonoga_kind_is_integer(left)) {
        return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                                "%s only works on f32 or f64, %s uses eq or "
                                "not_eq",
                                shown, bosonoga_value_kind_name(left));
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
  const BosonogaToken left_token = parser->tokenizer.token;
  parser->reads_new = TRUE;
  if (!bosonoga_parse_value(parser, &left, error)) {
    return FALSE;
  }
  BosonogaToken operation = parser->tokenizer.token;
  if (bosonoga_at_compare(parser, &compare)) {
    if (!bosonoga_advance(parser, error) ||
        !bosonoga_parse_value(parser, &right, error) ||
        !bosonoga_check_compare(parser, &operation, compare, left.literal.kind,
                                right.literal.kind, error)) {
      return FALSE;
    }
  } else if (left.literal.kind == BOSONOGA_VALUE_BOOL) {
    operation = left_token;
    memset(&right, 0, sizeof(right));
    right.literal.kind = BOSONOGA_VALUE_BOOL;
    right.literal.boolean = TRUE;
  } else {
    return bosonoga_parse_error(parser, "a comparison", error);
  }
  parser->reads_new = FALSE;

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
