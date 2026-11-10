#include "parser/internal.h"

static const struct {
  const gchar *word;
  BosonogaOpKind op;
  gboolean is_binary;
  gboolean accepts_integer;
} bosonoga_conversions[] = {
    {"to_i32", BOSONOGA_OP_CONVERT_TO_I32, FALSE, TRUE},
    {"to_i64", BOSONOGA_OP_CONVERT_TO_I64, FALSE, TRUE},
    {"to_u32", BOSONOGA_OP_CONVERT_TO_U32, FALSE, TRUE},
    {"to_u64", BOSONOGA_OP_CONVERT_TO_U64, FALSE, TRUE},
    {"to_f32", BOSONOGA_OP_CONVERT_TO_F32, FALSE, TRUE},
    {"to_f64", BOSONOGA_OP_CONVERT_TO_F64, FALSE, TRUE},
    {"to_abs", BOSONOGA_OP_ABS, FALSE, TRUE},
    {"to_min", BOSONOGA_OP_MIN, TRUE, TRUE},
    {"to_max", BOSONOGA_OP_MAX, TRUE, TRUE},
    {"to_sin", BOSONOGA_OP_SIN, FALSE, FALSE},
    {"to_cos", BOSONOGA_OP_COS, FALSE, FALSE},
    {"to_atan2", BOSONOGA_OP_ATAN2, TRUE, FALSE},
    {"to_sqrt", BOSONOGA_OP_SQRT, FALSE, FALSE},
    {"to_pow", BOSONOGA_OP_POW, TRUE, FALSE},
    {"to_floor", BOSONOGA_OP_FLOOR, FALSE, FALSE},
    {"to_ceil", BOSONOGA_OP_CEIL, FALSE, FALSE},
    {"to_round", BOSONOGA_OP_ROUND, FALSE, FALSE},
    {"to_log", BOSONOGA_OP_LOG, FALSE, FALSE},
    {"to_log10", BOSONOGA_OP_LOG10, FALSE, FALSE},
    {"to_exp", BOSONOGA_OP_EXP, FALSE, FALSE},
    {"to_asin", BOSONOGA_OP_ASIN, FALSE, FALSE},
    {"to_acos", BOSONOGA_OP_ACOS, FALSE, FALSE},
    {"to_tan", BOSONOGA_OP_TAN, FALSE, FALSE},
    {"to_log1p", BOSONOGA_OP_LOG1P, FALSE, FALSE},
    {"to_expm1", BOSONOGA_OP_EXPM1, FALSE, FALSE},
    {"to_hypot", BOSONOGA_OP_HYPOT, TRUE, FALSE},
};

static gint bosonoga_find_conversion(const BosonogaParser *parser) {
  for (gsize i = 0; i < G_N_ELEMENTS(bosonoga_conversions); ++i) {
    if (bosonoga_at_word(parser, bosonoga_conversions[i].word)) {
      return (gint)i;
    }
  }
  return -1;
}

gboolean bosonoga_at_conversion(const BosonogaParser *parser) {
  return bosonoga_find_conversion(parser) >= 0;
}

static BosonogaValueKind bosonoga_result_kind(BosonogaOpKind operation,
                                              BosonogaValueKind source) {
  switch ((gint)operation) {
    case BOSONOGA_OP_CONVERT_TO_I32:
      return BOSONOGA_VALUE_I32;
    case BOSONOGA_OP_CONVERT_TO_I64:
      return BOSONOGA_VALUE_I64;
    case BOSONOGA_OP_CONVERT_TO_U32:
      return BOSONOGA_VALUE_U32;
    case BOSONOGA_OP_CONVERT_TO_U64:
      return BOSONOGA_VALUE_U64;
    case BOSONOGA_OP_CONVERT_TO_F32:
      return BOSONOGA_VALUE_F32;
    case BOSONOGA_OP_CONVERT_TO_F64:
      return BOSONOGA_VALUE_F64;
    default:
      return source;
  }
}

gboolean bosonoga_parse_conversion(BosonogaParser *parser, guint slot,
                                   BosonogaValueKind *kind, GError **error) {
  const BosonogaToken start = parser->tokenizer.token;
  const gint found = bosonoga_find_conversion(parser);
  g_return_val_if_fail(found >= 0, FALSE);
  const gsize index = (gsize)found;
  const gchar *word = bosonoga_conversions[index].word;
  const gboolean accepts_integer = bosonoga_conversions[index].accepts_integer;
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  const BosonogaToken value = parser->tokenizer.token;
  BosonogaOperand left;
  BosonogaOperand right = {0};
  if (!bosonoga_parse_value(parser, &left, error)) {
    return FALSE;
  }
  const BosonogaValueKind source = left.literal.kind;
  if (!bosonoga_kind_is_real(source) &&
      !(accepts_integer && bosonoga_kind_is_integer(source))) {
    return bosonoga_fail_at(parser, &value, error, BOSONOGA_ERROR_TYPE,
                            "%s needs %s, got %s", word,
                            accepts_integer ? "a number" : "f32 or f64",
                            bosonoga_value_kind_name(source));
  }
  if (bosonoga_conversions[index].is_binary &&
      !bosonoga_parse_value_of_kind(parser, source, word, &right, error)) {
    return FALSE;
  }
  const BosonogaOpKind operation = bosonoga_conversions[index].op;
  *kind = bosonoga_result_kind(operation, source);
  BosonogaOp *op = bosonoga_emit(parser, operation, &start);
  op->slot = slot;
  op->left = left;
  op->right = right;
  return TRUE;
}
