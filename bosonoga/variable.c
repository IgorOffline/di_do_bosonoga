#include <string.h>

#include "bosonoga-private.h"

gboolean bosonoga_run_var(Regina* regina, gsize* position, GError** error) {
  const gsize start = *position;
  if (tokens_left(regina, start) < BOSONOGA_VAR_TOKEN_COUNT) {
    return fail_at(regina, start, error, BOSONOGA_ERROR_SYNTAX,
                   "'var' needs a name, 'is' and a value");
  }
  const gchar* name = token_at(regina, start + 1);
  if (!g_str_equal(token_at(regina, start + 2), "is")) {
    return fail_at(regina, start + 2, error, BOSONOGA_ERROR_SYNTAX,
                   "expected 'is' after 'var %s', found '%s'", name,
                   token_at(regina, start + 2));
  }
  if (bosonoga_index_of_variable(regina, name) != BOSONOGA_VARIABLE_NOT_FOUND) {
    return fail_at(regina, start + 1, error, BOSONOGA_ERROR_NAME,
                   "variable '%s' already exists", name);
  }
  if (regina->variable_count == BOSONOGA_VARIABLE_LIMIT) {
    return fail_at(regina, start + 1, error, BOSONOGA_ERROR_LIMIT,
                   "more than %" G_GSIZE_FORMAT " variables",
                   (gsize)BOSONOGA_VARIABLE_LIMIT);
  }

  const gchar* value = token_at(regina, start + 3);
  guint32 flags;
  if (bosonoga_parse_flags(regina, value, &flags)) {
    gsize next = start + BOSONOGA_VAR_TOKEN_COUNT;
    while (next < regina->token_count &&
           g_str_equal(token_at(regina, next), "shift_or")) {
      guint32 operand;
      if (tokens_left(regina, next) < BOSONOGA_SHIFT_OR_TOKEN_COUNT) {
        return fail_at(regina, next, error, BOSONOGA_ERROR_SYNTAX,
                       "'shift_or' needs a value");
      }
      if (!bosonoga_parse_flags_at(regina, next + 1, "shift_or", &operand,
                                   error)) {
        return FALSE;
      }
      flags |= operand;
      next += BOSONOGA_SHIFT_OR_TOKEN_COUNT;
    }
    BosonogaVariable* variable = bosonoga_add_variable(regina, name);
    variable->value.kind = BOSONOGA_KIND_FLAGS;
    variable->value.flags = flags;
    *position = next;
    return TRUE;
  }

  BosonogaValue initial;
  gsize consumed = BOSONOGA_VAR_TOKEN_COUNT;
  if (bosonoga_is_conversion(value)) {
    if (tokens_left(regina, start) < BOSONOGA_CONVERT_TOKEN_COUNT) {
      return fail_at(regina, start + 3, error, BOSONOGA_ERROR_SYNTAX,
                     "'%s' needs a value", value);
    }
    if (!bosonoga_convert_value(regina, value, start + 4, &initial, error)) {
      return FALSE;
    }
    consumed = BOSONOGA_CONVERT_TOKEN_COUNT;
  } else if (bosonoga_parse_i32_literal(value, &initial.i32)) {
    initial.kind = BOSONOGA_KIND_I32;
  } else if (bosonoga_parse_f32_literal(value, &initial.f32)) {
    initial.kind = BOSONOGA_KIND_F32;
  } else if (bosonoga_index_of_variable(regina, value) !=
             BOSONOGA_VARIABLE_NOT_FOUND) {
    return fail_at(regina, start + 3, error, BOSONOGA_ERROR_SYNTAX,
                   "'var' needs a literal or a conversion, not the "
                   "variable '%s'",
                   value);
  } else {
    return bosonoga_fail_bad_value(regina, start + 3, error);
  }

  BosonogaVariable* variable = bosonoga_add_variable(regina, name);
  variable->value = initial;
  *position += consumed;
  return TRUE;
}

gboolean bosonoga_run_set(Regina* regina, gsize* position, GError** error) {
  const gsize start = *position;
  if (tokens_left(regina, start) < BOSONOGA_SET_MIN_TOKEN_COUNT) {
    return fail_at(regina, start, error, BOSONOGA_ERROR_SYNTAX,
                   "'set' needs a name, an operation and a value");
  }
  const gchar* target_name = token_at(regina, start + 1);
  const gchar* operation = token_at(regina, start + 2);
  const gsize target = bosonoga_index_of_variable(regina, target_name);
  if (target == BOSONOGA_VARIABLE_NOT_FOUND) {
    return fail_at(regina, start + 1, error, BOSONOGA_ERROR_NAME,
                   "unknown variable '%s'", target_name);
  }
  BosonogaValue* destination = &regina->variables[target].value;

  if (bosonoga_is_conversion(operation)) {
    const gsize next = start + BOSONOGA_CONVERT_SET_TOKEN_COUNT;
    BosonogaValue converted;
    if (!bosonoga_convert_value(regina, operation, start + 3, &converted,
                                error)) {
      return FALSE;
    }
    if (converted.kind != destination->kind) {
      return fail_at(regina, start + 1, error, BOSONOGA_ERROR_TYPE,
                     "'%s' is %s, but %s gives %s", target_name,
                     bosonoga_kind_name(destination->kind), operation,
                     bosonoga_kind_name(converted.kind));
    }
    if (next < regina->token_count &&
        !bosonoga_is_statement_keyword(token_at(regina, next))) {
      return fail_at(regina, next, error, BOSONOGA_ERROR_SYNTAX,
                     "%s takes exactly one value, found '%s'", operation,
                     token_at(regina, next));
    }
    *destination = converted;
    *position = next;
    return TRUE;
  }

  const gboolean subtract = g_str_equal(operation, "to_subtract");
  if (!subtract && !g_str_equal(operation, "to_add")) {
    return fail_at(regina, start + 2, error, BOSONOGA_ERROR_SYNTAX,
                   "unknown operation '%s'; expected to_add, to_subtract, "
                   "to_f32 or to_i32",
                   operation);
  }
  if (destination->kind == BOSONOGA_KIND_FLAGS) {
    return fail_at(regina, start + 1, error, BOSONOGA_ERROR_TYPE,
                   "%s does not work on the flags variable '%s'", operation,
                   target_name);
  }
  const gboolean is_f32 = destination->kind == BOSONOGA_KIND_F32;

  gsize next = start + 3;
  gsize param_count = 0;
  gint64 whole = 0;
  gdouble real = 0.0;
  while (next < regina->token_count &&
         !bosonoga_is_statement_keyword(token_at(regina, next))) {
    BosonogaValue value;
    if (param_count == BOSONOGA_PARAM_LIMIT) {
      return fail_at(regina, next, error, BOSONOGA_ERROR_LIMIT,
                     "%s takes at most %d values", operation,
                     BOSONOGA_PARAM_LIMIT);
    }
    if (!bosonoga_parse_value_at(regina, next, &value, error)) {
      return FALSE;
    }
    if (value.kind != destination->kind) {
      return fail_at(regina, next, error, BOSONOGA_ERROR_TYPE,
                     "'%s' is %s, but '%s' is %s", token_at(regina, next),
                     bosonoga_kind_name(value.kind), target_name,
                     bosonoga_kind_name(destination->kind));
    }
    const gboolean minus = subtract && param_count > 0;
    if (is_f32) {
      real += minus ? -(gdouble)value.f32 : (gdouble)value.f32;
    } else {
      whole += minus ? -(gint64)value.i32 : (gint64)value.i32;
    }
    param_count++;
    next++;
  }

  if (param_count == 0) {
    return fail_at(regina, start + 2, error, BOSONOGA_ERROR_SYNTAX,
                   "%s needs at least one value", operation);
  }
  if (subtract && param_count == 1) {
    whole = -whole;
    real = -real;
  }
  if (is_f32) {
    if (!(real >= -(gdouble)G_MAXFLOAT && real <= (gdouble)G_MAXFLOAT)) {
      return fail_at(regina, start, error, BOSONOGA_ERROR_RANGE,
                     "the result is outside the f32 range");
    }
    destination->f32 = (gfloat)real;
  } else {
    if (whole < G_MININT32 || whole > G_MAXINT32) {
      return fail_at(
          regina, start, error, BOSONOGA_ERROR_RANGE,
          "the result (%" G_GINT64_FORMAT ") is outside the i32 range", whole);
    }
    destination->i32 = (gint32)whole;
  }
  *position = next;
  return TRUE;
}
