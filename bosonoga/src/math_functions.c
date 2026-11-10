#include <math.h>

#include "arithmetic.h"

static gboolean bosonoga_math_fail(GError **error, const gchar *message) {
  g_set_error_literal(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME, message);
  return FALSE;
}

// Not through gdouble, which cannot hold every i64 and u64.
static gboolean bosonoga_whole_math(BosonogaOpKind function,
                                    const BosonogaValue *left,
                                    const BosonogaValue *right,
                                    BosonogaValue *result, GError **error) {
  const BosonogaWide a = bosonoga_whole(left);
  BosonogaWide whole = a;
  switch ((gint)function) {
    case BOSONOGA_OP_ABS:
      whole = a < 0 ? -a : a;
      break;
    case BOSONOGA_OP_MIN:
      whole = MIN(a, bosonoga_whole(right));
      break;
    case BOSONOGA_OP_MAX:
      whole = MAX(a, bosonoga_whole(right));
      break;
    default:
      g_return_val_if_reached(FALSE);
  }
  if (!bosonoga_set_whole(result, left->kind, whole)) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
                "the result is outside the %s range",
                bosonoga_value_kind_name(left->kind));
    return FALSE;
  }
  return TRUE;
}

// NULL if a is in the domain of the function.
static const gchar *bosonoga_domain_error(BosonogaOpKind function, gdouble a,
                                          gdouble b) {
  switch ((gint)function) {
    case BOSONOGA_OP_ASIN:
      return a < -1.0 || a > 1.0 ? "to_asin needs a value from -1 to 1" : NULL;
    case BOSONOGA_OP_ACOS:
      return a < -1.0 || a > 1.0 ? "to_acos needs a value from -1 to 1" : NULL;
    case BOSONOGA_OP_SQRT:
      return a < 0.0 ? "to_sqrt of a negative number" : NULL;
    case BOSONOGA_OP_POW:
      if (a < 0.0 && trunc(b) != b) {
        return "to_pow of a negative number to a fractional power";
      }
      if (!(a < 0.0 || a > 0.0) && b < 0.0) {
        return "to_pow of zero to a negative power is division by zero";
      }
      return NULL;
    case BOSONOGA_OP_LOG:
    case BOSONOGA_OP_LOG10:
      return a <= 0.0 ? "logarithms need a positive value" : NULL;
    case BOSONOGA_OP_LOG1P:
      return a <= -1.0 ? "to_log1p needs a value greater than -1" : NULL;
    default:
      return NULL;
  }
}

static gdouble bosonoga_real_function(BosonogaOpKind function, gdouble a,
                                      gdouble b) {
  switch ((gint)function) {
    case BOSONOGA_OP_ABS:
      return fabs(a);
    case BOSONOGA_OP_MIN:
      return fmin(a, b);
    case BOSONOGA_OP_MAX:
      return fmax(a, b);
    case BOSONOGA_OP_FLOOR:
      return floor(a);
    case BOSONOGA_OP_CEIL:
      return ceil(a);
    case BOSONOGA_OP_ROUND:
      return round(a);
    case BOSONOGA_OP_SIN:
      return sin(a);
    case BOSONOGA_OP_COS:
      return cos(a);
    case BOSONOGA_OP_TAN:
      return tan(a);
    case BOSONOGA_OP_ASIN:
      return asin(a);
    case BOSONOGA_OP_ACOS:
      return acos(a);
    case BOSONOGA_OP_ATAN2:
      return atan2(a, b);
    case BOSONOGA_OP_SQRT:
      return sqrt(a);
    case BOSONOGA_OP_POW:
      return pow(a, b);
    case BOSONOGA_OP_LOG:
      return log(a);
    case BOSONOGA_OP_LOG10:
      return log10(a);
    case BOSONOGA_OP_EXP:
      return exp(a);
    case BOSONOGA_OP_LOG1P:
      return log1p(a);
    case BOSONOGA_OP_EXPM1:
      return expm1(a);
    case BOSONOGA_OP_HYPOT:
      return hypot(a, b);
    default:
      g_return_val_if_reached(NAN);
  }
}

gboolean bosonoga_math(BosonogaOpKind function, const BosonogaValue *left,
                       const BosonogaValue *right, BosonogaValue *result,
                       GError **error) {
  if (bosonoga_kind_is_integer(left->kind)) {
    return bosonoga_whole_math(function, left, right, result, error);
  }
  const gdouble a = bosonoga_real(left);
  // Only the binary functions have a right value.
  const gdouble b =
      bosonoga_kind_is_real(right->kind) ? bosonoga_real(right) : 0.0;
  const gchar *domain = bosonoga_domain_error(function, a, b);
  if (domain != NULL) {
    return bosonoga_math_fail(error, domain);
  }
  if (!bosonoga_set_real(result, left->kind,
                         bosonoga_real_function(function, a, b))) {
    return bosonoga_math_fail(error, left->kind == BOSONOGA_VALUE_F32
                                         ? "the result is outside the f32 range"
                                         : "the result is outside the f64 "
                                           "range");
  }
  return TRUE;
}
