#include <string.h>

#include "parser/internal.h"

static gint bosonoga_find_function_in(const BosonogaParser *parser,
                                      const BosonogaToken *token,
                                      gboolean visible_only) {
  if (token->kind != BOSONOGA_TOKEN_WORD) {
    return -1;
  }
  const BosonogaVariable *variable =
      visible_only ? bosonoga_find_variable(parser, token)
                   : bosonoga_find_variable_any(parser, token);
  if (variable != NULL) {
    return variable->is_array ? (gint)variable->function : -1;
  }
  for (guint i = 0; i < parser->functions->len; ++i) {
    const BosonogaFunction *function =
        &g_array_index(parser->functions, BosonogaFunction, i);
    if (!function->is_var && function->name_length == token->length &&
        memcmp(function->name, token->text, token->length) == 0) {
      return !visible_only ||
                     bosonoga_is_visible(
                         parser, &g_array_index(parser->function_declarations,
                                                BosonogaDeclaration, i))
                 ? (gint)i
                 : -1;
    }
  }
  return -1;
}

gint bosonoga_find_function(const BosonogaParser *parser,
                            const BosonogaToken *token) {
  return bosonoga_find_function_in(parser, token, TRUE);
}

gint bosonoga_find_function_any(const BosonogaParser *parser,
                                const BosonogaToken *token) {
  return bosonoga_find_function_in(parser, token, FALSE);
}

const gchar *bosonoga_function_noun(const BosonogaFunction *function) {
  return function->is_array ? "array" : "function";
}

const gchar *bosonoga_function_gives(const BosonogaFunction *function) {
  return function->is_array ? "has rows of" : "returns";
}

gboolean bosonoga_check_new_function_name(const BosonogaParser *parser,
                                          const BosonogaToken *name,
                                          const gchar *what, GError **error) {
  if (name->kind != BOSONOGA_TOKEN_WORD ||
      bosonoga_token_is_reserved(parser, name)) {
    return bosonoga_parse_error(parser, what, error);
  }
  g_autofree gchar *shown = bosonoga_token_shown(name);
  const gint found = bosonoga_find_function_any(parser, name);
  if (found >= 0) {
    return bosonoga_fail_at(
        parser, name, error, BOSONOGA_ERROR_NAME, "%s '%s' already exists",
        bosonoga_function_noun(
            &g_array_index(parser->functions, BosonogaFunction, (guint)found)),
        shown);
  }
  if (bosonoga_find_constant_any(parser, name) != NULL) {
    return bosonoga_fail_at(parser, name, error, BOSONOGA_ERROR_NAME,
                            "'%s' is already a constant", shown);
  }
  if (bosonoga_find_variable_any(parser, name) != NULL) {
    return bosonoga_fail_at(parser, name, error, BOSONOGA_ERROR_NAME,
                            "'%s' is already a variable", shown);
  }
  return TRUE;
}

gchar *bosonoga_values_shown(const BosonogaValueKind *kinds, guint count) {
  if (count == 0) {
    return g_strdup("no values");
  }
  GString *shown = g_string_new(NULL);
  g_string_append_printf(shown, "%u %s,", count,
                         count == 1 ? "value" : "values");
  for (guint i = 0; i < count; ++i) {
    g_string_append_printf(shown, " %s", bosonoga_value_kind_name(kinds[i]));
  }
  return g_string_free(shown, FALSE);
}

gboolean bosonoga_split_indexed(const gchar *text, gsize length, guint64 *index,
                                BosonogaValueKind *kind) {
  gsize digits = 0;
  while (digits < length && g_ascii_isdigit(text[digits])) {
    ++digits;
  }
  if (digits == 0 || digits > 9 || digits == length || text[digits] != '_' ||
      (digits > 1 && text[0] == '0')) {
    return FALSE;
  }
  *index = 0;
  for (gsize i = 0; i < digits; ++i) {
    *index = *index * 10 + (guint64)(text[i] - '0');
  }
  return bosonoga_value_kind_named(text + digits + 1, length - digits - 1,
                                   kind);
}

gboolean bosonoga_token_indexed(const BosonogaToken *token, guint64 *index,
                                BosonogaValueKind *kind) {
  if (token->kind != BOSONOGA_TOKEN_INDEXED_TYPE &&
      !(token->kind == BOSONOGA_TOKEN_LITERAL &&
        bosonoga_kind_is_number(token->value.kind))) {
    return FALSE;
  }
  return bosonoga_split_indexed(token->text, token->length, index, kind);
}

gboolean bosonoga_parse_types(BosonogaParser *parser, BosonogaValueKind *kinds,
                              guint *count, const gchar *too_many,
                              GError **error) {
  do {
    const BosonogaToken *token = &parser->tokenizer.token;
    guint64 index = 0;
    BosonogaValueKind kind = BOSONOGA_VALUE_I32;
    if (!bosonoga_token_indexed(token, &index, &kind)) {
      return bosonoga_parse_error(parser,
                                  "a type with its index, like 0_i32, of i32, "
                                  "i64, u32, u64, f32, f64, flags, color, str "
                                  "or bool",
                                  error);
    }
    if (*count == BOSONOGA_FUNCTION_VALUES_MAX) {
      return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_SYNTAX, "%s",
                              too_many);
    }
    if (index != *count) {
      g_autofree gchar *shown = bosonoga_token_shown(token);
      return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_SYNTAX,
                              "the values are numbered from 0 in order, so "
                              "'%s' is %u_%s",
                              shown, *count, bosonoga_value_kind_name(kind));
    }
    kinds[(*count)++] = kind;
    if (!bosonoga_advance(parser, error)) {
      return FALSE;
    }
  } while (!bosonoga_at_word(parser, "di") &&
           !bosonoga_at_word(parser, "rows"));
  return TRUE;
}

// The type is after an underscore, as in count_i32, and a type can have one
// of its own, as in pressed_input_key, so the longest type is taken, as
// xml_str in reply_xml_str.
static gboolean bosonoga_parameter_kind(const BosonogaToken *token,
                                        BosonogaValueKind *kind) {
  if (token->kind != BOSONOGA_TOKEN_WORD) {
    return FALSE;
  }
  for (gsize start = 2; start < token->length; ++start) {
    if (token->text[start - 1] == '_' &&
        bosonoga_value_kind_named(token->text + start, token->length - start,
                                  kind)) {
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean bosonoga_parse_function_rest(BosonogaParser *parser,
                                             BosonogaFunction *function,
                                             const BosonogaToken *name,
                                             GError **error) {
  while (!bosonoga_at_word(parser, "ret") &&
         !bosonoga_at_word(parser, BOSONOGA_VOID_WORD) &&
         !bosonoga_at_word(parser, "di")) {
    BosonogaValueKind kind = BOSONOGA_VALUE_I32;
    if (!bosonoga_parameter_kind(&parser->tokenizer.token, &kind)) {
      return bosonoga_parse_error(
          parser, "a parameter whose name ends with its type, like count_i32",
          error);
    }
    if (function->parameter_count == BOSONOGA_FUNCTION_VALUES_MAX) {
      return bosonoga_fail_at(
          parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_SYNTAX,
          "a function takes at most %d values", BOSONOGA_FUNCTION_VALUES_MAX);
    }
    BosonogaToken parameter;
    if (!bosonoga_parse_new_name(parser, &parameter, error)) {
      return FALSE;
    }
    function->parameters[function->parameter_count++] = kind;
    bosonoga_declare_variable(parser, &parameter, kind, parser->slot_count++);
  }

  if (bosonoga_at_word(parser, "ret")) {
    g_autofree gchar *too_many = g_strdup_printf(
        "a function returns at most %d values", BOSONOGA_FUNCTION_VALUES_MAX);
    if (!bosonoga_advance(parser, error) ||
        !bosonoga_parse_types(parser, function->results,
                              &function->result_count, too_many, error)) {
      return FALSE;
    }
  } else if (bosonoga_at_word(parser, BOSONOGA_VOID_WORD)) {
    if (!bosonoga_advance(parser, error)) {
      return FALSE;
    }
  } else {
    g_autofree gchar *shown = bosonoga_token_shown(name);
    return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                            BOSONOGA_ERROR_SYNTAX,
                            "'%s' must say what it returns: ret and its types, "
                            "or " BOSONOGA_VOID_WORD " for nothing",
                            shown);
  }

  // It is defined before its block, so that the block can call it.
  function->entry = parser->ops->len;
  const guint index = parser->functions->len;
  bosonoga_append_function(parser, function, name);
  parser->function = index;
  if (!bosonoga_parse_block(parser, error) ||
      !bosonoga_check_variables_used(parser, 0, error)) {
    return FALSE;
  }
  bosonoga_emit(parser,
                function->result_count > 0 ? BOSONOGA_OP_MISSING_RETURN
                                           : BOSONOGA_OP_FUNCTION_RETURN,
                name);
  g_array_index(parser->functions, BosonogaFunction, index).slot_count =
      parser->slot_count;
  return TRUE;
}

gboolean bosonoga_parse_function(BosonogaParser *parser, GError **error) {
  if (!bosonoga_expect_word(parser, "def", error)) {
    return FALSE;
  }
  const BosonogaToken name = parser->tokenizer.token;
  if (name.kind == BOSONOGA_TOKEN_WORD &&
      !bosonoga_token_is_reserved(parser, &name) &&
      !bosonoga_token_is_function_name(&name)) {
    if (bosonoga_token_is_word(&name, BOSONOGA_FUNCTION_SUFFIX)) {
      return bosonoga_fail_at(
          parser, &name, error, BOSONOGA_ERROR_NAME,
          "the name of a function has more before " BOSONOGA_FUNCTION_SUFFIX
          ", as in "
          "double" BOSONOGA_FUNCTION_SUFFIX);
    }
    g_autofree gchar *shown = bosonoga_token_shown(&name);
    return bosonoga_fail_at(
        parser, &name, error, BOSONOGA_ERROR_NAME,
        "the name of a function ends with " BOSONOGA_FUNCTION_SUFFIX
        ", as in %s" BOSONOGA_FUNCTION_SUFFIX,
        // The old fn_ prefix, without it in the suggestion.
        g_str_has_prefix(shown, "fn_") && shown[3] != '\0' ? shown + 3 : shown);
  }
  if (!bosonoga_check_new_function_name(parser, &name, "a function name",
                                        error) ||
      !bosonoga_advance(parser, error)) {
    return FALSE;
  }

  BosonogaFunction function;
  memset(&function, 0, sizeof(function));
  function.name = name.text;
  function.name_length = name.length;

  g_autoptr(GArray) variables =
      g_array_new(FALSE, FALSE, sizeof(BosonogaVariable));
  GArray *outer_variables = parser->variables;
  const guint outer_slot_count = parser->slot_count;
  const BosonogaSection outer_section = parser->section;
  parser->variables = variables;
  parser->slot_count = 0;
  parser->section = BOSONOGA_SECTION_MAIN;
  parser->in_function = TRUE;
  const gboolean parsed =
      bosonoga_parse_function_rest(parser, &function, &name, error);
  parser->in_function = FALSE;
  parser->section = outer_section;
  parser->slot_count = outer_slot_count;
  parser->variables = outer_variables;
  return parsed;
}
