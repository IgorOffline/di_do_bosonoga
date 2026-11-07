#include "parser/internal.h"

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
  if (left == DARIA_VALUE_COLOR) {
    return daria_fail_at(parser, token, error, DARIA_ERROR_TYPE,
                         "colors cannot be compared");
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

// Each group of "and" jumps to the next group as soon as a comparison is false,
// and to the true case when it reaches its end, so that "and" binds tighter.
gboolean daria_parse_condition(DariaParser *parser, GArray *false_jumps,
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
