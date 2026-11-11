#include "arithmetic.h"

#include <math.h>
#include <string.h>

#include "key.h"

#define BOSONOGA_TOLERANCE_3 1e-3
#define BOSONOGA_TOLERANCE_6 1e-6
// Every integer type is inside it, and every real inside it converts exactly.
#define BOSONOGA_WHOLE_REAL_MAX 0x1p65

static const gchar *bosonoga_format_real(gchar text[G_ASCII_DTOSTR_BUF_SIZE],
                                         const gchar *format, gdouble value) {
  g_ascii_formatd(text, G_ASCII_DTOSTR_BUF_SIZE, format, value);
  return g_strdelimit(text, ".", 'd');
}

void bosonoga_print_value(GString *output, const BosonogaValue *value) {
  gchar text[MAX(G_ASCII_DTOSTR_BUF_SIZE, BOSONOGA_WIDE_TEXT_SIZE)];
  switch (value->kind) {
    case BOSONOGA_VALUE_I32:
    case BOSONOGA_VALUE_I64:
    case BOSONOGA_VALUE_U32:
    case BOSONOGA_VALUE_U64:
      g_string_append(output, bosonoga_wide_text(text, bosonoga_whole(value)));
      break;
    case BOSONOGA_VALUE_F32:
      g_string_append(output,
                      bosonoga_format_real(text, "%.6g", (gdouble)value->f32));
      break;
    case BOSONOGA_VALUE_F64:
      g_string_append(output, bosonoga_format_real(text, "%.17g", value->f64));
      break;
    case BOSONOGA_VALUE_FLAGS:
      g_string_append_printf(output, "%" G_GUINT32_FORMAT, value->flags);
      break;
    case BOSONOGA_VALUE_COLOR:
      g_string_append_printf(output, "%06" G_GINT32_MODIFIER "X", value->color);
      break;
    case BOSONOGA_VALUE_STR:
      g_string_append_len(output, value->str.text, (gssize)value->str.length);
      break;
    case BOSONOGA_VALUE_BOOL:
      g_string_append(output, value->boolean ? "true" : "false");
      break;
    case BOSONOGA_VALUE_INPUT_KEY:
      g_string_append(output, bosonoga_key_name(value->input_key));
      break;
    case BOSONOGA_VALUE_FONT:
      g_string_append(output, value->font.text);
      break;
  }
}

static gboolean bosonoga_real_close(const BosonogaValue *left,
                                    const BosonogaValue *right,
                                    gdouble tolerance) {
  const gdouble a = bosonoga_real(left);
  const gdouble b = bosonoga_real(right);
  const gdouble difference = ABS(a - b);
  return difference <= tolerance ||
         (MAX(ABS(a), ABS(b)) > 0.0 &&
          difference / MAX(ABS(a), ABS(b)) <= tolerance);
}

// -1, 0 or 1, of two integers or two reals, which are never NaN.
static gint bosonoga_order(const BosonogaValue *left,
                           const BosonogaValue *right) {
  if (bosonoga_kind_is_integer(left->kind)) {
    const BosonogaWide a = bosonoga_whole(left);
    const BosonogaWide b = bosonoga_whole(right);
    return a < b ? -1 : a > b;
  }
  const gdouble a = bosonoga_real(left);
  const gdouble b = bosonoga_real(right);
  return a < b ? -1 : a > b;
}

static gboolean bosonoga_equal(const BosonogaValue *left,
                               const BosonogaValue *right) {
  if (left->kind == BOSONOGA_VALUE_STR) {
    return left->str.length == right->str.length &&
           memcmp(left->str.text, right->str.text, left->str.length) == 0;
  }
  if (left->kind == BOSONOGA_VALUE_BOOL) {
    return left->boolean == right->boolean;
  }
  if (left->kind == BOSONOGA_VALUE_INPUT_KEY) {
    return left->input_key == right->input_key;
  }
  if (left->kind == BOSONOGA_VALUE_FONT) {
    return left->font.index == right->font.index;
  }
  return bosonoga_order(left, right) == 0;
}

gboolean bosonoga_compare_values(BosonogaCompareKind compare,
                                 const BosonogaValue *left,
                                 const BosonogaValue *right) {
  switch (compare) {
    case BOSONOGA_COMPARE_GT:
      return bosonoga_order(left, right) > 0;
    case BOSONOGA_COMPARE_LT:
      return bosonoga_order(left, right) < 0;
    case BOSONOGA_COMPARE_GE:
      return bosonoga_order(left, right) >= 0;
    case BOSONOGA_COMPARE_LE:
      return bosonoga_order(left, right) <= 0;
    case BOSONOGA_COMPARE_EQ:
      return bosonoga_equal(left, right);
    case BOSONOGA_COMPARE_NOT_EQ:
      return !bosonoga_equal(left, right);
    case BOSONOGA_COMPARE_EQ_3:
      return bosonoga_real_close(left, right, BOSONOGA_TOLERANCE_3);
    case BOSONOGA_COMPARE_EQ_6:
      return bosonoga_real_close(left, right, BOSONOGA_TOLERANCE_6);
    case BOSONOGA_COMPARE_NOT_EQ_3:
      return !bosonoga_real_close(left, right, BOSONOGA_TOLERANCE_3);
    case BOSONOGA_COMPARE_NOT_EQ_6:
      return !bosonoga_real_close(left, right, BOSONOGA_TOLERANCE_6);
    case BOSONOGA_COMPARE_SHIFT_HAS:
      return (left->flags & right->flags) == right->flags;
  }
  g_return_val_if_reached(FALSE);
}

void bosonoga_accumulator_load(BosonogaAccumulator *accumulator,
                               const BosonogaValue *value) {
  if (bosonoga_kind_is_integer(value->kind)) {
    accumulator->whole = bosonoga_whole(value);
  } else {
    accumulator->real = bosonoga_real(value);
  }
}

static gboolean bosonoga_fail_range(const BosonogaValue *value,
                                    const gchar *shown, GError **error) {
  g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
              "the result%s%s%s is outside the %s range",
              shown == NULL ? "" : " (", shown == NULL ? "" : shown,
              shown == NULL ? "" : ")", bosonoga_value_kind_name(value->kind));
  return FALSE;
}

// Each step is checked, as the accumulator itself can overflow.
static gboolean bosonoga_whole_step(BosonogaWide *whole, BosonogaOpKind step,
                                    BosonogaWide value, gboolean *by_zero) {
  switch ((gint)step) {
    case BOSONOGA_OP_ACCUMULATOR_ADD:
      return !__builtin_add_overflow(*whole, value, whole);
    case BOSONOGA_OP_ACCUMULATOR_SUBTRACT:
      return !__builtin_sub_overflow(*whole, value, whole);
    case BOSONOGA_OP_ACCUMULATOR_MULTIPLY:
      return !__builtin_mul_overflow(*whole, value, whole);
    case BOSONOGA_OP_ACCUMULATOR_DIVIDE:
    case BOSONOGA_OP_ACCUMULATOR_MOD:
      *by_zero = value == 0;
      if (*by_zero) {
        return FALSE;
      }
      // The smallest BosonogaWide divided by -1 does not fit.
      if (value == -1) {
        return step == BOSONOGA_OP_ACCUMULATOR_MOD
                   ? (*whole = 0, TRUE)
                   : !__builtin_sub_overflow(0, *whole, whole);
      }
      *whole =
          step == BOSONOGA_OP_ACCUMULATOR_MOD ? *whole % value : *whole / value;
      return TRUE;
    default:
      g_return_val_if_reached(FALSE);
  }
}

gboolean bosonoga_accumulator_step(BosonogaAccumulator *accumulator,
                                   BosonogaOpKind step,
                                   const BosonogaValue *value, GError **error) {
  if (bosonoga_kind_is_integer(value->kind)) {
    gboolean by_zero = FALSE;
    if (bosonoga_whole_step(&accumulator->whole, step, bosonoga_whole(value),
                            &by_zero)) {
      return TRUE;
    }
    if (by_zero) {
      g_set_error_literal(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
                          "division by zero");
      return FALSE;
    }
    return bosonoga_fail_range(value, NULL, error);
  }
  const gdouble real = bosonoga_real(value);
  switch ((gint)step) {
    case BOSONOGA_OP_ACCUMULATOR_ADD:
      accumulator->real += real;
      return TRUE;
    case BOSONOGA_OP_ACCUMULATOR_SUBTRACT:
      accumulator->real -= real;
      return TRUE;
    case BOSONOGA_OP_ACCUMULATOR_MULTIPLY:
      accumulator->real *= real;
      return TRUE;
    case BOSONOGA_OP_ACCUMULATOR_DIVIDE:
    case BOSONOGA_OP_ACCUMULATOR_MOD:
      if (!(real < 0.0 || real > 0.0)) {
        g_set_error_literal(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
                            "division by zero");
        return FALSE;
      }
      accumulator->real = step == BOSONOGA_OP_ACCUMULATOR_MOD
                              ? fmod(accumulator->real, real)
                              : accumulator->real / real;
      return TRUE;
    default:
      g_return_val_if_reached(FALSE);
  }
}

gboolean bosonoga_accumulator_store(const BosonogaAccumulator *accumulator,
                                    BosonogaValue *slot, GError **error) {
  if (bosonoga_kind_is_integer(slot->kind)) {
    gchar text[BOSONOGA_WIDE_TEXT_SIZE];
    return bosonoga_set_whole(slot, slot->kind, accumulator->whole) ||
           bosonoga_fail_range(
               slot, bosonoga_wide_text(text, accumulator->whole), error);
  }
  gchar text[G_ASCII_DTOSTR_BUF_SIZE];
  return bosonoga_set_real(slot, slot->kind, accumulator->real) ||
         bosonoga_fail_range(
             slot, bosonoga_format_real(text, "%.6g", accumulator->real),
             error);
}

static BosonogaValueKind bosonoga_converted_kind(BosonogaOpKind conversion) {
  switch ((gint)conversion) {
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
      g_return_val_if_reached(BOSONOGA_VALUE_I32);
  }
}

// A real converts to an integer toward zero.
gboolean bosonoga_convert(BosonogaOpKind conversion, const BosonogaValue *value,
                          BosonogaValue *result, GError **error) {
  const BosonogaValueKind kind = bosonoga_converted_kind(conversion);
  gboolean fits = FALSE;
  if (bosonoga_kind_is_real(kind)) {
    fits = bosonoga_set_real(result, kind, bosonoga_real(value));
  } else if (bosonoga_kind_is_integer(value->kind)) {
    fits = bosonoga_set_whole(result, kind, bosonoga_whole(value));
  } else {
    const gdouble whole = trunc(bosonoga_real(value));
    fits = ABS(whole) < BOSONOGA_WHOLE_REAL_MAX &&
           bosonoga_set_whole(result, kind, (BosonogaWide)whole);
  }
  if (!fits) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
                "the value is outside the %s range",
                bosonoga_value_kind_name(kind));
  }
  return fits;
}
