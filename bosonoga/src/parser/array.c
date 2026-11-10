#include <string.h>

#include "parser/internal.h"

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
  } while (!bosonoga_at_word(parser, "do"));
  return bosonoga_advance(parser, error);
}

gboolean bosonoga_parse_array(BosonogaParser *parser, GError **error) {
  if (!bosonoga_expect_word(parser, "array", error)) {
    return FALSE;
  }
  const BosonogaToken name = parser->tokenizer.token;
  if (bosonoga_token_is_function_name(&name)) {
    g_autofree gchar *shown = bosonoga_token_shown(&name);
    return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_NAME,
                            "'%s' ends with " BOSONOGA_FUNCTION_SUFFIX
                            ", which only the name of a function does",
                            shown);
  }
  if (!bosonoga_check_new_function_name(parser, &name, "an array name",
                                        error) ||
      !bosonoga_advance(parser, error)) {
    return FALSE;
  }

  BosonogaFunction array;
  memset(&array, 0, sizeof(array));
  array.name = name.text;
  array.name_length = name.length;
  array.is_array = TRUE;
  array.parameter_count = 1;
  array.parameters[0] = BOSONOGA_VALUE_I32;
  array.first_value = parser->program->rows->len;
  g_autofree gchar *too_many = g_strdup_printf(
      "a row of an array has at most %d values", BOSONOGA_FUNCTION_VALUES_MAX);
  if (!bosonoga_parse_types(parser, array.results, &array.result_count,
                            too_many, error)) {
    return FALSE;
  }

  // So that a row cannot have gfx_delta_time, after gfx_frame.
  const BosonogaSection outer_section = parser->section;
  parser->section = BOSONOGA_SECTION_MAIN;
  const gboolean parsed = bosonoga_parse_rows(parser, &array, error);
  parser->section = outer_section;
  if (!parsed) {
    return FALSE;
  }
  g_array_append_val(parser->functions, array);
  return TRUE;
}

gboolean bosonoga_parse_array_read(BosonogaParser *parser,
                                   const BosonogaToken *name, guint found,
                                   GError **error) {
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
  BosonogaOperand index;
  if (!bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_I32, context, &index,
                                    error)) {
    return FALSE;
  }
  if (bosonoga_at_value(parser)) {
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
