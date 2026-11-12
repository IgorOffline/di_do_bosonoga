#include <string.h>

#include "parser/internal.h"

static gboolean bosonoga_check_array_size(const BosonogaParser *parser,
                                          const BosonogaToken *token,
                                          const BosonogaFunction *array,
                                          guint64 row_count, GError **error) {
  if (row_count * array->result_count <= BOSONOGA_ARRAY_VALUES_MAX) {
    return TRUE;
  }
  return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                          "'%.*s' would hold %" G_GUINT64_FORMAT
                          " values, and an array holds at most %d",
                          (gint)array->name_length, array->name,
                          row_count * array->result_count,
                          BOSONOGA_ARRAY_VALUES_MAX);
}

static gboolean bosonoga_parse_rows(BosonogaParser *parser,
                                    BosonogaFunction *array, GError **error) {
  g_autofree gchar *name = g_strndup(array->name, array->name_length);
  g_autofree gchar *types =
      bosonoga_values_shown(array->results, array->result_count);
  g_autofree gchar *context = g_strdup_printf("a row of '%s'", name);
  if (!bosonoga_expect_word(parser, "di", error)) {
    return FALSE;
  }
  do {
    const BosonogaToken row = parser->tokenizer.token;
    if (!bosonoga_expect_word(parser, "row", error)) {
      return FALSE;
    }
    for (guint i = 0; i < array->result_count; ++i) {
      if (!bosonoga_at_value(parser)) {
        return bosonoga_fail_at(parser, &row, error, BOSONOGA_ERROR_TYPE,
                                "'%s' has rows of %s, but this row has %u",
                                name, types, i);
      }
      const BosonogaToken *token = &parser->tokenizer.token;
      if (token->kind == BOSONOGA_TOKEN_WORD &&
          !bosonoga_token_is_reserved(parser, token) &&
          bosonoga_find_constant(parser, token) == NULL) {
        g_autofree gchar *shown = bosonoga_token_shown(token);
        return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_SYNTAX,
                                "the values of a row are literals, like 7_i32, "
                                "so a row cannot have '%s'",
                                shown);
      }
      BosonogaOperand value;
      if (!bosonoga_parse_value_of_kind(parser, array->results[i], context,
                                        &value, error)) {
        return FALSE;
      }
      g_array_append_val(parser->program->rows, value.literal);
    }
    if (bosonoga_at_value(parser)) {
      return bosonoga_fail_at(parser, &row, error, BOSONOGA_ERROR_TYPE,
                              "'%s' has rows of %s, but this row has more",
                              name, types);
    }
    ++array->row_count;
    if (array->is_var && !bosonoga_check_array_size(parser, &row, array,
                                                    array->row_count, error)) {
      return FALSE;
    }
  } while (!bosonoga_at_word(parser, "do"));
  return bosonoga_advance(parser, error);
}

static gboolean bosonoga_parse_array_types(BosonogaParser *parser,
                                           const BosonogaToken *name,
                                           BosonogaFunction *array,
                                           GError **error) {
  memset(array, 0, sizeof(*array));
  array->name = name->text;
  array->name_length = name->length;
  array->is_array = TRUE;
  array->parameter_count = 1;
  array->parameters[0] = BOSONOGA_VALUE_I32;
  array->first_value = parser->program->rows->len;
  if (!bosonoga_expect_word(parser, "array", error)) {
    return FALSE;
  }
  g_autofree gchar *too_many = g_strdup_printf(
      "a row of an array has at most %d values", BOSONOGA_FUNCTION_VALUES_MAX);
  return bosonoga_parse_types(parser, array->results, &array->result_count,
                              too_many, error);
}

// So that a row cannot have gfx_delta_time, after gfx_frame.
static gboolean bosonoga_parse_literal_rows(BosonogaParser *parser,
                                            BosonogaFunction *array,
                                            GError **error) {
  const BosonogaSection outer_section = parser->section;
  parser->section = BOSONOGA_SECTION_MAIN;
  const gboolean parsed = bosonoga_parse_rows(parser, array, error);
  parser->section = outer_section;
  return parsed;
}

gboolean bosonoga_parse_val_array(BosonogaParser *parser,
                                  const BosonogaToken *name, GError **error) {
  if (!bosonoga_at_word(parser, "array")) {
    return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                            BOSONOGA_ERROR_SYNTAX,
                            "a top-level val is an array, as in val task is "
                            "array 0_i32, and const binds a value");
  }
  BosonogaFunction array;
  if (!bosonoga_parse_array_types(parser, name, &array, error)) {
    return FALSE;
  }
  if (bosonoga_at_word(parser, "rows")) {
    return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                            BOSONOGA_ERROR_SYNTAX,
                            "a val array has literal rows, and only a var "
                            "array has zeroed ones");
  }
  if (!bosonoga_parse_literal_rows(parser, &array, error)) {
    return FALSE;
  }
  g_array_append_val(parser->functions, array);
  return TRUE;
}

static gboolean bosonoga_append_zero_row(BosonogaParser *parser,
                                         const BosonogaToken *token,
                                         BosonogaFunction *array,
                                         GError **error) {
  for (guint i = 0; i < array->result_count; ++i) {
    const BosonogaValueKind kind = array->results[i];
    if (kind == BOSONOGA_VALUE_INPUT_KEY || kind == BOSONOGA_VALUE_FONT ||
        kind == BOSONOGA_VALUE_ICON) {
      return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_TYPE,
                              "a zeroed row cannot have %s, which has no zero, "
                              "but literal rows can",
                              bosonoga_value_kind_name(kind));
    }
    BosonogaValue zero;
    memset(&zero, 0, sizeof(zero));
    zero.kind = kind;
    if (kind == BOSONOGA_VALUE_STR) {
      zero.str.text = "";
    }
    g_array_append_val(parser->program->rows, zero);
  }
  array->is_zeroed = TRUE;
  return TRUE;
}

gboolean bosonoga_parse_var_array(BosonogaParser *parser,
                                  const BosonogaToken *name, GError **error) {
  BosonogaFunction array;
  if (!bosonoga_parse_array_types(parser, name, &array, error)) {
    return FALSE;
  }
  array.is_var = TRUE;
  if (bosonoga_at_word(parser, "rows")) {
    const BosonogaToken rows = parser->tokenizer.token;
    if (!bosonoga_advance(parser, error)) {
      return FALSE;
    }
    const BosonogaToken count_token = parser->tokenizer.token;
    BosonogaOperand count;
    if (!bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_I32, "rows",
                                      &count, error)) {
      return FALSE;
    }
    if (count.is_variable || count.literal.i32 < 1) {
      return bosonoga_fail_at(parser, &count_token, error, BOSONOGA_ERROR_TYPE,
                              "rows needs an i32 literal or constant above "
                              "zero, as the size of an array is known before "
                              "the program runs");
    }
    if (!bosonoga_check_array_size(parser, &count_token, &array,
                                   (guint64)count.literal.i32, error) ||
        !bosonoga_append_zero_row(parser, &rows, &array, error)) {
      return FALSE;
    }
    array.row_count = (guint)count.literal.i32;
  } else if (!bosonoga_parse_literal_rows(parser, &array, error)) {
    return FALSE;
  }

  array.slot = parser->slot_count;
  parser->slot_count += array.row_count * array.result_count;
  const guint index = parser->functions->len;
  g_array_append_val(parser->functions, array);
  bosonoga_emit(parser, BOSONOGA_OP_ARRAY_INIT, name)->function = index;
  bosonoga_declare_variable(parser, name, BOSONOGA_VALUE_I32, array.slot);
  BosonogaVariable *variable = &g_array_index(
      parser->variables, BosonogaVariable, parser->variables->len - 1);
  variable->is_array = TRUE;
  variable->function = index;
  return TRUE;
}

static gboolean bosonoga_parse_index(BosonogaParser *parser,
                                     const BosonogaToken *name,
                                     BosonogaOperand *index, GError **error) {
  g_autofree gchar *shown = bosonoga_token_shown(name);
  if (!bosonoga_at_word(parser, "at")) {
    return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                            BOSONOGA_ERROR_SYNTAX,
                            "'%s' is an array, which is read at one i32 "
                            "index, as in %s at 0_i32",
                            shown, shown);
  }
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  g_autofree gchar *context = g_strdup_printf("the index of '%s'", shown);
  return bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_I32, context,
                                      index, error);
}

gboolean bosonoga_parse_array_read(BosonogaParser *parser,
                                   const BosonogaToken *name, guint found,
                                   GError **error) {
  BosonogaOperand index;
  if (!bosonoga_parse_index(parser, name, &index, error)) {
    return FALSE;
  }
  if (bosonoga_at_value(parser)) {
    g_autofree gchar *shown = bosonoga_token_shown(name);
    return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                            BOSONOGA_ERROR_TYPE,
                            "'%s' is an array, which is read at one i32 "
                            "index, but is given more",
                            shown);
  }
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_ARRAY_READ, name);
  op->function = found;
  op->left = index;
  return TRUE;
}

gboolean bosonoga_parse_array_write(BosonogaParser *parser,
                                    const BosonogaToken *name,
                                    const BosonogaVariable *variable,
                                    GError **error) {
  const BosonogaFunction array =
      g_array_index(parser->functions, BosonogaFunction, variable->function);
  BosonogaOperand index;
  if (!bosonoga_parse_index(parser, name, &index, error) ||
      !bosonoga_expect_word(parser, "to", error)) {
    return FALSE;
  }
  g_autofree gchar *shown = bosonoga_token_shown(name);
  g_autofree gchar *types =
      bosonoga_values_shown(array.results, array.result_count);
  g_autofree gchar *context = g_strdup_printf("a row of '%s'", shown);
  for (guint i = 0; i < array.result_count; ++i) {
    const BosonogaToken value_token = parser->tokenizer.token;
    if (!bosonoga_at_value(parser)) {
      return bosonoga_fail_at(parser, name, error, BOSONOGA_ERROR_TYPE,
                              "'%s' has rows of %s, but this row has %u", shown,
                              types, i);
    }
    BosonogaOperand value;
    if (!bosonoga_parse_value_of_kind(parser, array.results[i], context, &value,
                                      error)) {
      return FALSE;
    }
    BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_ARGUMENT, &value_token);
    op->index = i;
    op->left = value;
  }
  if (bosonoga_at_value(parser)) {
    return bosonoga_fail_at(
        parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_TYPE,
        "'%s' has rows of %s, but this row has more", shown, types);
  }
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_ARRAY_WRITE, name);
  op->function = variable->function;
  op->left = index;
  return TRUE;
}

gboolean bosonoga_at_len(const BosonogaParser *parser) {
  return bosonoga_at_word(parser, "len_col") ||
         bosonoga_at_word(parser, "len_row");
}

gboolean bosonoga_parse_len(BosonogaParser *parser, BosonogaValue *value,
                            GError **error) {
  const gboolean is_col = bosonoga_at_word(parser, "len_col");
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  const BosonogaToken *name = &parser->tokenizer.token;
  const gint found = bosonoga_find_function(parser, name);
  if (found < 0 ||
      !g_array_index(parser->functions, BosonogaFunction, (guint)found)
           .is_array) {
    return bosonoga_parse_error(parser, "the name of an array", error);
  }
  const BosonogaFunction *array =
      &g_array_index(parser->functions, BosonogaFunction, (guint)found);
  memset(value, 0, sizeof(*value));
  value->kind = BOSONOGA_VALUE_I32;
  value->i32 = (gint32)(is_col ? array->row_count : array->result_count);
  return bosonoga_advance(parser, error);
}
