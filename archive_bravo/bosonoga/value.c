#include <string.h>

#include "bosonoga-private.h"

const gchar* bosonoga_kind_name(BosonogaKind kind) {
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

gsize bosonoga_index_of_variable(const Regina* regina, const gchar* name) {
  for (gsize i = 0; i < regina->variable_count; i++) {
    if (g_str_equal(regina->variables[i].name, name)) {
      return i;
    }
  }
  return BOSONOGA_VARIABLE_NOT_FOUND;
}

BosonogaVariable* bosonoga_add_variable(Regina* regina, const gchar* name) {
  BosonogaVariable* variable = &regina->variables[regina->variable_count++];
  g_strlcpy(variable->name, name, sizeof variable->name);
  return variable;
}

gboolean bosonoga_parse_i32(const gchar* text, gsize length, gint32* result) {
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

gboolean bosonoga_parse_i32_literal(const gchar* token, gint32* result) {
  return g_str_has_suffix(token, BOSONOGA_I32_SUFFIX) &&
         bosonoga_parse_i32(token, strlen(token) - strlen(BOSONOGA_I32_SUFFIX),
                            result);
}

gboolean bosonoga_parse_f32_literal(const gchar* token, gfloat* result) {
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

gboolean bosonoga_parse_flags(const Regina* regina, const gchar* token,
                              guint32* result) {
  const gsize index = bosonoga_index_of_variable(regina, token);
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

gboolean bosonoga_parse_value(const Regina* regina, const gchar* token,
                              BosonogaValue* result) {
  const gsize index = bosonoga_index_of_variable(regina, token);
  if (index != BOSONOGA_VARIABLE_NOT_FOUND) {
    *result = regina->variables[index].value;
    return TRUE;
  }
  if (bosonoga_parse_i32_literal(token, &result->i32)) {
    result->kind = BOSONOGA_KIND_I32;
    return TRUE;
  }
  if (bosonoga_parse_f32_literal(token, &result->f32)) {
    result->kind = BOSONOGA_KIND_F32;
    return TRUE;
  }
  if (bosonoga_parse_flags(regina, token, &result->flags)) {
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

gboolean bosonoga_fail_bad_value(const Regina* regina, gsize position,
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

gboolean bosonoga_parse_value_at(const Regina* regina, gsize position,
                                 BosonogaValue* result, GError** error) {
  return bosonoga_parse_value(regina, token_at(regina, position), result) ||
         bosonoga_fail_bad_value(regina, position, error);
}

gboolean bosonoga_parse_flags_at(const Regina* regina, gsize position,
                                 const gchar* operation, guint32* result,
                                 GError** error) {
  const gchar* token = token_at(regina, position);
  if (bosonoga_parse_flags(regina, token, result)) {
    return TRUE;
  }
  BosonogaValue value;
  if (bosonoga_parse_value(regina, token, &value)) {
    return fail_at(regina, position, error, BOSONOGA_ERROR_TYPE,
                   "%s needs flags, but '%s' is %s", operation, token,
                   bosonoga_kind_name(value.kind));
  }
  return bosonoga_fail_bad_value(regina, position, error);
}

gboolean bosonoga_is_conversion(const gchar* token) {
  return g_str_equal(token, "to_f32") || g_str_equal(token, "to_i32");
}

gboolean bosonoga_convert_value(const Regina* regina, const gchar* operation,
                                gsize source_position, BosonogaValue* result,
                                GError** error) {
  BosonogaValue source;
  if (!bosonoga_parse_value_at(regina, source_position, &source, error)) {
    return FALSE;
  }
  const gchar* source_token = token_at(regina, source_position);
  const BosonogaKind needed =
      g_str_equal(operation, "to_f32") ? BOSONOGA_KIND_I32 : BOSONOGA_KIND_F32;
  if (source.kind != needed) {
    return fail_at(regina, source_position, error, BOSONOGA_ERROR_TYPE,
                   "%s needs %s, but '%s' is %s", operation,
                   bosonoga_kind_name(needed), source_token,
                   bosonoga_kind_name(source.kind));
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

gboolean bosonoga_f32_close(gfloat left, gfloat right, gdouble tolerance) {
  const gdouble a = (gdouble)left;
  const gdouble b = (gdouble)right;
  const gdouble difference = ABS(a - b);
  const gdouble scale = MAX(ABS(a), ABS(b));
  return difference <= tolerance * scale || difference <= tolerance;
}

gboolean bosonoga_parse_return_status(const gchar* keyword, gint* status) {
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

gboolean bosonoga_is_statement_keyword(const gchar* token) {
  return g_str_equal(token, "var") || g_str_equal(token, "set") ||
         g_str_equal(token, "if") || g_str_equal(token, "else") ||
         g_str_has_prefix(token, BOSONOGA_LOOP_PREFIX) ||
         g_str_equal(token, "di") || g_str_equal(token, "do") ||
         g_str_equal(token, "return") || g_str_equal(token, "nihil");
}
