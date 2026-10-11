#include <string.h>

#include "bosonoga.h"

#define BOSONOGA_VARIABLE_NOT_FOUND BOSONOGA_VARIABLE_LIMIT
#define BOSONOGA_PARAM_LIMIT 8

#define BOSONOGA_VAR_TOKEN_COUNT 4
#define BOSONOGA_SET_MIN_TOKEN_COUNT 4
#define BOSONOGA_SHIFT_OR_TOKEN_COUNT 2
#define BOSONOGA_COMPARISON_TOKEN_COUNT 3
#define BOSONOGA_CONVERT_TOKEN_COUNT 5
#define BOSONOGA_CONVERT_SET_TOKEN_COUNT 4
#define BOSONOGA_LOOP_MIN_TOKEN_COUNT 4
#define BOSONOGA_LOOP_WITH_MIN_TOKEN_COUNT 6

#define BOSONOGA_I32_SUFFIX "_i32"
#define BOSONOGA_F32_SUFFIX "_f32"
#define BOSONOGA_LOOP_PREFIX "to_"
#define BOSONOGA_I32_RANGE_LIMIT 2147483648.0f
#define BOSONOGA_F32_TOLERANCE_3 1e-3
#define BOSONOGA_F32_TOLERANCE_6 1e-6
#define BOSONOGA_SHIFT_PREFIX "shift_"
#define BOSONOGA_SHIFT_DIGIT_LIMIT 2
#define BOSONOGA_SHIFT_BIT_LIMIT 32

#define fail_at bosonoga_fail_at

static const gchar* kind_name(BosonogaKind kind) {
  switch (kind) {
    case BOSONOGA_KIND_I32:
      return "i32";
    case BOSONOGA_KIND_F32:
      return "f32";
    case BOSONOGA_KIND_FLAGS:
      return "flags";
  }
  g_return_val_if_reached("unknown");
}

static gsize index_of_variable(const Regina* regina, const gchar* name) {
  for (gsize i = 0; i < regina->variable_count; i++) {
    if (g_str_equal(regina->variables[i].name, name)) {
      return i;
    }
  }
  return BOSONOGA_VARIABLE_NOT_FOUND;
}

static BosonogaVariable* add_variable(Regina* regina, const gchar* name) {
  BosonogaVariable* variable = &regina->variables[regina->variable_count++];
  g_strlcpy(variable->name, name, sizeof variable->name);
  return variable;
}

static gboolean parse_i32(const gchar* text, gsize length, gint32* result) {
  gchar digits[BOSONOGA_TOKEN_LENGTH + 1];
  if (length == 0 || length > BOSONOGA_TOKEN_LENGTH || text[0] == '+') {
    return FALSE;
  }
  memcpy(digits, text, length);
  digits[length] = '\0';
  gint64 value = 0;
  if (!g_ascii_string_to_signed(digits, 10, G_MININT32, G_MAXINT32, &value,
                                NULL)) {
    return FALSE;
  }
  *result = (gint32)value;
  return TRUE;
}

static gboolean parse_i32_literal(const gchar* token, gint32* result) {
  return g_str_has_suffix(token, BOSONOGA_I32_SUFFIX) &&
         parse_i32(token, strlen(token) - strlen(BOSONOGA_I32_SUFFIX), result);
}

static gboolean parse_f32_literal(const gchar* token, gfloat* result) {
  if (!g_str_has_suffix(token, BOSONOGA_F32_SUFFIX)) {
    return FALSE;
  }
  const gsize length = strlen(token) - strlen(BOSONOGA_F32_SUFFIX);
  gsize i = token[0] == '-' ? 1 : 0;
  const gsize integer_start = i;
  while (i < length && g_ascii_isdigit(token[i])) {
    i++;
  }
  if (i == integer_start) {
    return FALSE;
  }
  if (i < length && token[i] == '.') {
    i++;
    const gsize fraction_start = i;
    while (i < length && g_ascii_isdigit(token[i])) {
      i++;
    }
    if (i == fraction_start) {
      return FALSE;
    }
  }
  if (i != length) {
    return FALSE;
  }

  gchar digits[BOSONOGA_TOKEN_LENGTH + 1];
  memcpy(digits, token, length);
  digits[length] = '\0';
  const gdouble value = g_ascii_strtod(digits, NULL);
  if (!(value >= -(gdouble)G_MAXFLOAT && value <= (gdouble)G_MAXFLOAT)) {
    return FALSE;
  }
  *result = (gfloat)value;
  return TRUE;
}

static gboolean parse_flags(const Regina* regina, const gchar* token,
                            guint32* result) {
  const gsize index = index_of_variable(regina, token);
  if (index != BOSONOGA_VARIABLE_NOT_FOUND) {
    const BosonogaVariable* variable = &regina->variables[index];
    if (variable->value.kind != BOSONOGA_KIND_FLAGS) {
      return FALSE;
    }
    *result = variable->value.flags;
    return TRUE;
  }

  if (!g_str_has_prefix(token, BOSONOGA_SHIFT_PREFIX)) {
    return FALSE;
  }
  const gchar* digits = token + strlen(BOSONOGA_SHIFT_PREFIX);
  const gsize digit_count = strlen(digits);
  if (digit_count == 0 || digit_count > BOSONOGA_SHIFT_DIGIT_LIMIT) {
    return FALSE;
  }
  guint32 bit = 0;
  for (gsize i = 0; i < digit_count; i++) {
    if (!g_ascii_isdigit(digits[i])) {
      return FALSE;
    }
    bit = bit * 10U + (guint32)g_ascii_digit_value(digits[i]);
  }
  if (bit >= BOSONOGA_SHIFT_BIT_LIMIT) {
    return FALSE;
  }
  *result = 1U << bit;
  return TRUE;
}

static gboolean parse_value(const Regina* regina, const gchar* token,
                            BosonogaValue* result) {
  const gsize index = index_of_variable(regina, token);
  if (index != BOSONOGA_VARIABLE_NOT_FOUND) {
    *result = regina->variables[index].value;
    return TRUE;
  }
  if (parse_i32_literal(token, &result->i32)) {
    result->kind = BOSONOGA_KIND_I32;
    return TRUE;
  }
  if (parse_f32_literal(token, &result->f32)) {
    result->kind = BOSONOGA_KIND_F32;
    return TRUE;
  }
  if (parse_flags(regina, token, &result->flags)) {
    result->kind = BOSONOGA_KIND_FLAGS;
    return TRUE;
  }
  return FALSE;
}

static gboolean is_bare_number(const gchar* token) {
  const gchar* digit = token[0] == '-' ? token + 1 : token;
  if (!g_ascii_isdigit(*digit)) {
    return FALSE;
  }
  for (; *digit != '\0'; digit++) {
    if (!g_ascii_isdigit(*digit) && *digit != '.') {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean fail_bad_value(const Regina* regina, gsize position,
                               GError** error) {
  const gchar* token = token_at(regina, position);
  if (strstr(token, "//") != NULL) {
    return fail_at(regina, position, error, BOSONOGA_ERROR_SYNTAX,
                   "'%s': a comment needs a space before '//'", token);
  }
  if (is_bare_number(token)) {
    return fail_at(regina, position, error, BOSONOGA_ERROR_SYNTAX,
                   "'%s' has no type suffix; write %s_i32 or %s_f32", token,
                   token, token);
  }
  if (g_str_has_suffix(token, BOSONOGA_I32_SUFFIX) ||
      g_str_has_suffix(token, BOSONOGA_F32_SUFFIX) ||
      g_str_has_prefix(token, BOSONOGA_SHIFT_PREFIX)) {
    return fail_at(regina, position, error, BOSONOGA_ERROR_SYNTAX,
                   "'%s' is not a valid literal", token);
  }
  return fail_at(regina, position, error, BOSONOGA_ERROR_NAME,
                 "unknown variable '%s'", token);
}

static gboolean parse_value_at(const Regina* regina, gsize position,
                               BosonogaValue* result, GError** error) {
  return parse_value(regina, token_at(regina, position), result) ||
         fail_bad_value(regina, position, error);
}

static gboolean parse_flags_at(const Regina* regina, gsize position,
                               const gchar* operation, guint32* result,
                               GError** error) {
  const gchar* token = token_at(regina, position);
  if (parse_flags(regina, token, result)) {
    return TRUE;
  }
  BosonogaValue value;
  if (parse_value(regina, token, &value)) {
    return fail_at(regina, position, error, BOSONOGA_ERROR_TYPE,
                   "%s needs flags, but '%s' is %s", operation, token,
                   kind_name(value.kind));
  }
  return fail_bad_value(regina, position, error);
}

static gboolean is_conversion(const gchar* token) {
  return g_str_equal(token, "to_f32") || g_str_equal(token, "to_i32");
}

static gboolean convert_value(const Regina* regina, const gchar* operation,
                              gsize source_position, BosonogaValue* result,
                              GError** error) {
  BosonogaValue source;
  if (!parse_value_at(regina, source_position, &source, error)) {
    return FALSE;
  }
  const gchar* source_token = token_at(regina, source_position);
  const BosonogaKind needed =
      g_str_equal(operation, "to_f32") ? BOSONOGA_KIND_I32 : BOSONOGA_KIND_F32;
  if (source.kind != needed) {
    return fail_at(regina, source_position, error, BOSONOGA_ERROR_TYPE,
                   "%s needs %s, but '%s' is %s", operation, kind_name(needed),
                   source_token, kind_name(source.kind));
  }
  if (needed == BOSONOGA_KIND_I32) {
    result->kind = BOSONOGA_KIND_F32;
    result->f32 = (gfloat)source.i32;
    return TRUE;
  }
  if (!(source.f32 >= -BOSONOGA_I32_RANGE_LIMIT &&
        source.f32 < BOSONOGA_I32_RANGE_LIMIT)) {
    return fail_at(regina, source_position, error, BOSONOGA_ERROR_RANGE,
                   "'%s' (%g) is outside the i32 range", source_token,
                   (gdouble)source.f32);
  }
  result->kind = BOSONOGA_KIND_I32;
  result->i32 = (gint32)source.f32;
  return TRUE;
}

static gboolean f32_close(gfloat left, gfloat right, gdouble tolerance) {
  const gdouble a = (gdouble)left;
  const gdouble b = (gdouble)right;
  const gdouble difference = ABS(a - b);
  const gdouble scale = MAX(ABS(a), ABS(b));
  return difference <= tolerance * scale || difference <= tolerance;
}

static gboolean parse_return_status(const gchar* keyword, gint* status) {
  if (g_str_equal(keyword, "exit_success")) {
    *status = BOSONOGA_EXIT_SUCCESS;
    return TRUE;
  }
  if (g_str_equal(keyword, "exit_failure")) {
    *status = BOSONOGA_EXIT_FAILURE;
    return TRUE;
  }
  return FALSE;
}

static gboolean is_statement_keyword(const gchar* token) {
  return g_str_equal(token, "var") || g_str_equal(token, "set") ||
         g_str_equal(token, "if") || g_str_equal(token, "else") ||
         g_str_has_prefix(token, BOSONOGA_LOOP_PREFIX) ||
         g_str_equal(token, "di") || g_str_equal(token, "do") ||
         g_str_equal(token, "return") || g_str_equal(token, "nihil");
}

static gboolean run_var(Regina* regina, gsize* position, GError** error) {
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
  if (index_of_variable(regina, name) != BOSONOGA_VARIABLE_NOT_FOUND) {
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
  if (parse_flags(regina, value, &flags)) {
    gsize next = start + BOSONOGA_VAR_TOKEN_COUNT;
    while (next < regina->token_count &&
           g_str_equal(token_at(regina, next), "shift_or")) {
      guint32 operand;
      if (tokens_left(regina, next) < BOSONOGA_SHIFT_OR_TOKEN_COUNT) {
        return fail_at(regina, next, error, BOSONOGA_ERROR_SYNTAX,
                       "'shift_or' needs a value");
      }
      if (!parse_flags_at(regina, next + 1, "shift_or", &operand, error)) {
        return FALSE;
      }
      flags |= operand;
      next += BOSONOGA_SHIFT_OR_TOKEN_COUNT;
    }
    BosonogaVariable* variable = add_variable(regina, name);
    variable->value.kind = BOSONOGA_KIND_FLAGS;
    variable->value.flags = flags;
    *position = next;
    return TRUE;
  }

  BosonogaValue initial;
  gsize consumed = BOSONOGA_VAR_TOKEN_COUNT;
  if (is_conversion(value)) {
    if (tokens_left(regina, start) < BOSONOGA_CONVERT_TOKEN_COUNT) {
      return fail_at(regina, start + 3, error, BOSONOGA_ERROR_SYNTAX,
                     "'%s' needs a value", value);
    }
    if (!convert_value(regina, value, start + 4, &initial, error)) {
      return FALSE;
    }
    consumed = BOSONOGA_CONVERT_TOKEN_COUNT;
  } else if (parse_i32_literal(value, &initial.i32)) {
    initial.kind = BOSONOGA_KIND_I32;
  } else if (parse_f32_literal(value, &initial.f32)) {
    initial.kind = BOSONOGA_KIND_F32;
  } else if (index_of_variable(regina, value) != BOSONOGA_VARIABLE_NOT_FOUND) {
    return fail_at(regina, start + 3, error, BOSONOGA_ERROR_SYNTAX,
                   "'var' needs a literal or a conversion, not the "
                   "variable '%s'",
                   value);
  } else {
    return fail_bad_value(regina, start + 3, error);
  }

  BosonogaVariable* variable = add_variable(regina, name);
  variable->value = initial;
  *position += consumed;
  return TRUE;
}

static gboolean run_set(Regina* regina, gsize* position, GError** error) {
  const gsize start = *position;
  if (tokens_left(regina, start) < BOSONOGA_SET_MIN_TOKEN_COUNT) {
    return fail_at(regina, start, error, BOSONOGA_ERROR_SYNTAX,
                   "'set' needs a name, an operation and a value");
  }
  const gchar* target_name = token_at(regina, start + 1);
  const gchar* operation = token_at(regina, start + 2);
  const gsize target = index_of_variable(regina, target_name);
  if (target == BOSONOGA_VARIABLE_NOT_FOUND) {
    return fail_at(regina, start + 1, error, BOSONOGA_ERROR_NAME,
                   "unknown variable '%s'", target_name);
  }
  BosonogaValue* destination = &regina->variables[target].value;

  if (is_conversion(operation)) {
    const gsize next = start + BOSONOGA_CONVERT_SET_TOKEN_COUNT;
    BosonogaValue converted;
    if (!convert_value(regina, operation, start + 3, &converted, error)) {
      return FALSE;
    }
    if (converted.kind != destination->kind) {
      return fail_at(regina, start + 1, error, BOSONOGA_ERROR_TYPE,
                     "'%s' is %s, but %s gives %s", target_name,
                     kind_name(destination->kind), operation,
                     kind_name(converted.kind));
    }
    if (next < regina->token_count &&
        !is_statement_keyword(token_at(regina, next))) {
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
         !is_statement_keyword(token_at(regina, next))) {
    BosonogaValue value;
    if (param_count == BOSONOGA_PARAM_LIMIT) {
      return fail_at(regina, next, error, BOSONOGA_ERROR_LIMIT,
                     "%s takes at most %d values", operation,
                     BOSONOGA_PARAM_LIMIT);
    }
    if (!parse_value_at(regina, next, &value, error)) {
      return FALSE;
    }
    if (value.kind != destination->kind) {
      return fail_at(regina, next, error, BOSONOGA_ERROR_TYPE,
                     "'%s' is %s, but '%s' is %s", token_at(regina, next),
                     kind_name(value.kind), target_name,
                     kind_name(destination->kind));
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

static gboolean evaluate_comparison(const Regina* regina, gsize position,
                                    gboolean* comparison, GError** error) {
  const gchar* operation = token_at(regina, position + 1);

  if (g_str_equal(operation, "shift_has")) {
    guint32 left;
    guint32 right;
    if (!parse_flags_at(regina, position, operation, &left, error) ||
        !parse_flags_at(regina, position + 2, operation, &right, error)) {
      return FALSE;
    }
    *comparison = (left & right) == right;
    return TRUE;
  }
  if (g_str_equal(operation, "shift_eq")) {
    return fail_at(regina, position + 1, error, BOSONOGA_ERROR_SYNTAX,
                   "shift_eq was renamed shift_has");
  }

  BosonogaValue left;
  BosonogaValue right;
  if (!parse_value_at(regina, position, &left, error) ||
      !parse_value_at(regina, position + 2, &right, error)) {
    return FALSE;
  }
  if (left.kind != right.kind) {
    return fail_at(regina, position + 1, error, BOSONOGA_ERROR_TYPE,
                   "cannot compare %s '%s' with %s '%s'", kind_name(left.kind),
                   token_at(regina, position), kind_name(right.kind),
                   token_at(regina, position + 2));
  }
  if (left.kind == BOSONOGA_KIND_FLAGS) {
    return fail_at(regina, position + 1, error, BOSONOGA_ERROR_TYPE,
                   "flags can only be compared with shift_has");
  }

  if (left.kind == BOSONOGA_KIND_F32) {
    if (g_str_equal(operation, "gt")) {
      *comparison = left.f32 > right.f32;
    } else if (g_str_equal(operation, "lt")) {
      *comparison = left.f32 < right.f32;
    } else if (g_str_equal(operation, "eq_3")) {
      *comparison = f32_close(left.f32, right.f32, BOSONOGA_F32_TOLERANCE_3);
    } else if (g_str_equal(operation, "eq_6")) {
      *comparison = f32_close(left.f32, right.f32, BOSONOGA_F32_TOLERANCE_6);
    } else if (g_str_equal(operation, "not_eq_3")) {
      *comparison = !f32_close(left.f32, right.f32, BOSONOGA_F32_TOLERANCE_3);
    } else if (g_str_equal(operation, "not_eq_6")) {
      *comparison = !f32_close(left.f32, right.f32, BOSONOGA_F32_TOLERANCE_6);
    } else if (g_str_equal(operation, "eq") ||
               g_str_equal(operation, "not_eq")) {
      return fail_at(regina, position + 1, error, BOSONOGA_ERROR_TYPE,
                     "f32 has no exact %s; use %s_3 or %s_6", operation,
                     operation, operation);
    } else {
      return fail_at(regina, position + 1, error, BOSONOGA_ERROR_SYNTAX,
                     "unknown comparison '%s'", operation);
    }
    return TRUE;
  }

  if (g_str_equal(operation, "gt")) {
    *comparison = left.i32 > right.i32;
  } else if (g_str_equal(operation, "lt")) {
    *comparison = left.i32 < right.i32;
  } else if (g_str_equal(operation, "eq")) {
    *comparison = left.i32 == right.i32;
  } else if (g_str_equal(operation, "not_eq")) {
    *comparison = left.i32 != right.i32;
  } else if (g_str_has_prefix(operation, "eq_") ||
             g_str_has_prefix(operation, "not_eq_")) {
    return fail_at(regina, position + 1, error, BOSONOGA_ERROR_TYPE,
                   "%s only works on f32; i32 uses eq / not_eq", operation);
  } else {
    return fail_at(regina, position + 1, error, BOSONOGA_ERROR_SYNTAX,
                   "unknown comparison '%s'", operation);
  }
  return TRUE;
}

static gboolean find_block_end(const Regina* regina, gsize opening, gsize end,
                               gsize* closing, GError** error) {
  const gsize start = opening + 1;
  gsize depth = 1;
  for (gsize position = start; position < end; position++) {
    const gchar* token = token_at(regina, position);
    if (g_str_equal(token, "di")) {
      depth++;
    } else if (g_str_equal(token, "do")) {
      depth--;
      if (depth == 0) {
        if (position == start) {
          return fail_at(regina, opening, error, BOSONOGA_ERROR_SYNTAX,
                         "empty block; write nihil for a block that does "
                         "nothing");
        }
        *closing = position;
        return TRUE;
      }
    }
  }
  return fail_at(regina, opening, error, BOSONOGA_ERROR_SYNTAX,
                 "'di' has no matching 'do'");
}

static gsize position_after_main(const Regina* regina) {
  for (gsize position = 0; position < regina->token_count; position++) {
    if (g_str_equal(token_at(regina, position), "main")) {
      return position + 1;
    }
  }
  return regina->token_count;
}

static gboolean run_body(Regina* regina, gsize position, gsize end,
                         GError** error);

static gboolean evaluate_condition(const Regina* regina, gsize* position,
                                   gsize end, gboolean* result,
                                   GError** error) {
  gsize next = *position;
  gboolean condition = FALSE;
  gboolean and_group = TRUE;
  while (TRUE) {
    if (end - next < BOSONOGA_COMPARISON_TOKEN_COUNT) {
      return fail_at(regina, next, error, BOSONOGA_ERROR_SYNTAX,
                     "incomplete condition");
    }
    gboolean comparison = FALSE;
    if (!evaluate_comparison(regina, next, &comparison, error)) {
      return FALSE;
    }
    and_group = and_group && comparison;
    next += BOSONOGA_COMPARISON_TOKEN_COUNT;
    if (next == end) {
      return fail_at(regina, next, error, BOSONOGA_ERROR_SYNTAX,
                     "the condition must be followed by 'di'");
    }

    const gchar* connector = token_at(regina, next);
    if (g_str_equal(connector, "or")) {
      condition = condition || and_group;
      and_group = TRUE;
    } else if (!g_str_equal(connector, "and")) {
      break;
    }
    next++;
  }
  *result = condition || and_group;
  *position = next;
  return TRUE;
}

static gboolean run_if(Regina* regina, gsize* position, gsize end,
                       GError** error) {
  gsize next = *position + 1;
  gboolean has_condition = TRUE;
  gboolean chosen = FALSE;
  gsize chosen_start = 0;
  gsize chosen_closing = 0;
  while (TRUE) {
    gboolean condition = TRUE;
    if (has_condition) {
      if (next < end && g_str_equal(token_at(regina, next), "di")) {
        return fail_at(regina, next, error, BOSONOGA_ERROR_SYNTAX,
                       "missing condition before 'di'");
      }
      if (!evaluate_condition(regina, &next, end, &condition, error)) {
        return FALSE;
      }
    }
    if (next >= end || !g_str_equal(token_at(regina, next), "di")) {
      return fail_at(regina, next, error, BOSONOGA_ERROR_SYNTAX,
                     "expected 'di', found '%s'",
                     next < end ? token_at(regina, next) : "nothing");
    }
    gsize closing;
    if (!find_block_end(regina, next, end, &closing, error)) {
      return FALSE;
    }
    if (!chosen && condition) {
      chosen = TRUE;
      chosen_start = next + 1;
      chosen_closing = closing;
    }
    next = closing + 1;

    if (!has_condition || next == end ||
        !g_str_equal(token_at(regina, next), "else")) {
      break;
    }
    next++;
    has_condition = next < end && g_str_equal(token_at(regina, next), "if");
    if (has_condition) {
      next++;
    }
  }

  gboolean ok = TRUE;
  if (chosen) {
    const gsize saved_count = regina->variable_count;
    ok = run_body(regina, chosen_start, chosen_closing, error);
    regina->variable_count = saved_count;
  }
  *position = next;
  return ok;
}

static gboolean run_loop(Regina* regina, gsize* position, gsize end,
                         GError** error) {
  const gsize start = *position;
  const gchar* keyword = token_at(regina, start);
  const gchar* count_text = keyword + strlen(BOSONOGA_LOOP_PREFIX);
  gint32 limit;
  if (!g_ascii_isdigit(*count_text) ||
      !parse_i32(count_text, strlen(count_text), &limit)) {
    return fail_at(regina, start, error, BOSONOGA_ERROR_SYNTAX,
                   "'%s' is not a statement; loops are written to_N, "
                   "e.g. to_8",
                   keyword);
  }
  if (end - start < BOSONOGA_LOOP_MIN_TOKEN_COUNT) {
    return fail_at(regina, start, error, BOSONOGA_ERROR_SYNTAX,
                   "'%s' needs a di ... do block", keyword);
  }
  gsize opening = start + 1;
  const gchar* name = NULL;
  if (g_str_equal(token_at(regina, opening), "with")) {
    if (end - start < BOSONOGA_LOOP_WITH_MIN_TOKEN_COUNT) {
      return fail_at(regina, opening, error, BOSONOGA_ERROR_SYNTAX,
                     "'with' needs a counter name and a di ... do block");
    }
    name = token_at(regina, opening + 1);
    opening += 2;
  }
  if (!g_str_equal(token_at(regina, opening), "di")) {
    return fail_at(regina, opening, error, BOSONOGA_ERROR_SYNTAX,
                   "expected 'di', found '%s'", token_at(regina, opening));
  }
  gsize closing;
  if (!find_block_end(regina, opening, end, &closing, error)) {
    return FALSE;
  }

  const gsize saved_count = regina->variable_count;
  BosonogaVariable* counter = NULL;
  if (name != NULL) {
    if (is_statement_keyword(name) || g_str_equal(name, "with")) {
      return fail_at(regina, opening - 1, error, BOSONOGA_ERROR_NAME,
                     "'%s' cannot be a loop counter", name);
    }
    if (index_of_variable(regina, name) != BOSONOGA_VARIABLE_NOT_FOUND) {
      return fail_at(regina, opening - 1, error, BOSONOGA_ERROR_NAME,
                     "variable '%s' already exists", name);
    }
    if (regina->variable_count == BOSONOGA_VARIABLE_LIMIT) {
      return fail_at(regina, opening - 1, error, BOSONOGA_ERROR_LIMIT,
                     "more than %" G_GSIZE_FORMAT " variables",
                     (gsize)BOSONOGA_VARIABLE_LIMIT);
    }
    counter = add_variable(regina, name);
    counter->value.kind = BOSONOGA_KIND_I32;
  }

  const gsize body_count = regina->variable_count;
  gboolean ok = TRUE;
  for (gint32 i = 0; i < limit && ok && !regina->returned; i++) {
    if (counter != NULL) {
      counter->value.i32 = i;
    }
    ok = run_body(regina, opening + 1, closing, error);
    regina->variable_count = body_count;
  }
  regina->variable_count = saved_count;
  *position = closing + 1;
  return ok;
}

static gboolean run_body(Regina* regina, gsize position, gsize end,
                         GError** error) {
  while (position < end) {
    const gchar* keyword = token_at(regina, position);
    gboolean ok;
    if (g_str_equal(keyword, "var")) {
      ok = run_var(regina, &position, error);
    } else if (g_str_equal(keyword, "set")) {
      ok = run_set(regina, &position, error);
    } else if (g_str_equal(keyword, "if")) {
      ok = run_if(regina, &position, end, error);
    } else if (g_str_has_prefix(keyword, BOSONOGA_LOOP_PREFIX)) {
      ok = run_loop(regina, &position, end, error);
    } else if (g_str_equal(keyword, "nihil")) {
      position++;
      ok = TRUE;
    } else if (g_str_equal(keyword, "return")) {
      if (position + 1 >= end ||
          !parse_return_status(token_at(regina, position + 1),
                               &regina->exit_status)) {
        return fail_at(regina, position, error, BOSONOGA_ERROR_SYNTAX,
                       "'return' needs exit_success or exit_failure");
      }
      regina->returned = TRUE;
      return TRUE;
    } else if (g_str_equal(keyword, "else")) {
      return fail_at(regina, position, error, BOSONOGA_ERROR_SYNTAX,
                     "'else' without a matching 'if'");
    } else {
      return fail_at(regina, position, error, BOSONOGA_ERROR_SYNTAX,
                     "unknown statement '%s'", keyword);
    }
    if (!ok || regina->returned) {
      return ok;
    }
    if (position > end) {
      return fail_at(regina, end, error, BOSONOGA_ERROR_SYNTAX,
                     "a statement runs past the end of its block");
    }
  }
  return TRUE;
}

static gboolean run_main(Regina* regina, GError** error) {
  if (!run_body(regina, position_after_main(regina), regina->token_count,
                error)) {
    return FALSE;
  }
  if (!regina->returned) {
    return fail_at(regina, regina->token_count, error, BOSONOGA_ERROR_RUNTIME,
                   "the program ended without 'return'");
  }
  return TRUE;
}

static void log_regina(const Regina* regina) {
  g_debug("Regina: %" G_GSIZE_FORMAT " MiB, %" G_GSIZE_FORMAT
          " lines (%" G_GSIZE_FORMAT " header), %" G_GSIZE_FORMAT
          " tokens, %" G_GSIZE_FORMAT " variables",
          (gsize)(sizeof(Regina) / BOSONOGA_MIB), regina->line_count,
          regina->header_line_count, regina->token_count,
          regina->variable_count);
  for (gsize i = 0; i < regina->version_count; i++) {
    g_debug("version[%" G_GSIZE_FORMAT "]: %s", i, regina->version[i].string);
  }
  for (gsize i = 0; i < regina->lega_count; i++) {
    g_debug("lega[%" G_GSIZE_FORMAT "]: %s", i, regina->lega[i].legal);
  }
  for (gsize i = 0; i < regina->aka_count; i++) {
    g_debug("aka[%" G_GSIZE_FORMAT "]: %s -> %s", i, regina->aka[i].original,
            regina->aka[i].replacement);
  }
  for (gsize i = 0; i < regina->token_count; i++) {
    g_debug("tokens[%" G_GSIZE_FORMAT "]: %s", i, token_at(regina, i));
  }
  for (gsize i = 0; i < regina->variable_count; i++) {
    const BosonogaVariable* variable = &regina->variables[i];
    switch (variable->value.kind) {
      case BOSONOGA_KIND_FLAGS:
        g_debug("variables[%" G_GSIZE_FORMAT "]: %s = flags %" G_GUINT32_FORMAT,
                i, variable->name, variable->value.flags);
        break;
      case BOSONOGA_KIND_F32:
        g_debug("variables[%" G_GSIZE_FORMAT "]: %s = %.6g_f32", i,
                variable->name, (gdouble)variable->value.f32);
        break;
      case BOSONOGA_KIND_I32:
        g_debug("variables[%" G_GSIZE_FORMAT "]: %s = %" G_GINT32_FORMAT "_i32",
                i, variable->name, variable->value.i32);
        break;
    }
  }
}

static void reset_regina(Regina* regina) {
  regina->line_count = 0;
  regina->header_line_count = 0;
  regina->token_count = 0;
  regina->variable_count = 0;
  regina->version_count = 0;
  regina->lega_count = 0;
  regina->aka_count = 0;
  regina->main_count = 0;
  regina->returned = FALSE;
  regina->exit_status = BOSONOGA_EXIT_FAILURE;
}

gboolean bosonoga_core(Regina* regina, const gchar* header, const gchar* input,
                       gint* exit_status, GError** error) {
  g_return_val_if_fail(regina != NULL, FALSE);
  g_return_val_if_fail(header != NULL, FALSE);
  g_return_val_if_fail(input != NULL, FALSE);
  g_return_val_if_fail(exit_status != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  reset_regina(regina);
  const gboolean ok = bosonoga_load_program(regina, header, input, error) &&
                      run_main(regina, error);
  if (!g_log_writer_default_would_drop(G_LOG_LEVEL_DEBUG, G_LOG_DOMAIN)) {
    log_regina(regina);
  }
  if (ok) {
    *exit_status = regina->exit_status;
  }
  return ok;
}
