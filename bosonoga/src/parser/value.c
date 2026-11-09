#include <string.h>

#include "parser/internal.h"

#define BOSONOGA_SHIFT_BIT_MAX 31

const BosonogaVariable *bosonoga_find_variable(const BosonogaParser *parser,
                                               const BosonogaToken *token) {
  for (guint i = 0; i < parser->variables->len; ++i) {
    const BosonogaVariable *variable =
        &g_array_index(parser->variables, BosonogaVariable, i);
    if (variable->length == token->length &&
        memcmp(variable->name, token->text, token->length) == 0) {
      return variable;
    }
  }
  return NULL;
}

void bosonoga_declare_variable(BosonogaParser *parser,
                               const BosonogaToken *name,
                               BosonogaValueKind kind, guint slot) {
  const BosonogaVariable variable = {name->text, name->length, kind, slot};
  g_array_append_val(parser->variables, variable);
}

gboolean bosonoga_fail_unknown_variable(const BosonogaParser *parser,
                                        const BosonogaToken *name,
                                        GError **error) {
  g_autofree gchar *shown = bosonoga_token_shown(name);
  const gint found = bosonoga_find_function(parser, name);
  if (found >= 0) {
    const BosonogaFunction *function =
        &g_array_index(parser->functions, BosonogaFunction, (guint)found);
    return bosonoga_fail_at(parser, name, error, BOSONOGA_ERROR_NAME,
                            "'%s' is %s, which is %s with var, set ... to or "
                            "call",
                            shown,
                            function->is_array ? "an array" : "a function",
                            function->is_array ? "read" : "called");
  }
  if (bosonoga_token_is_function_name(name)) {
    return bosonoga_fail_at(parser, name, error, BOSONOGA_ERROR_NAME,
                            "unknown function '%s'", shown);
  }
  return bosonoga_fail_at(parser, name, error, BOSONOGA_ERROR_NAME,
                          "unknown variable '%s'", shown);
}

gboolean bosonoga_parse_new_name(BosonogaParser *parser, BosonogaToken *name,
                                 GError **error) {
  *name = parser->tokenizer.token;
  if (name->kind != BOSONOGA_TOKEN_WORD ||
      bosonoga_token_is_reserved(parser, name)) {
    return bosonoga_parse_error(parser, "a variable name", error);
  }
  if (bosonoga_find_constant(parser, name) != NULL) {
    return bosonoga_fail_at(parser, name, error, BOSONOGA_ERROR_NAME,
                            "a constant with this name already exists");
  }
  if (bosonoga_find_variable(parser, name) != NULL) {
    g_autofree gchar *shown = bosonoga_token_shown(name);
    return bosonoga_fail_at(parser, name, error, BOSONOGA_ERROR_NAME,
                            "variable '%s' already exists", shown);
  }
  if (bosonoga_token_is_function_name(name)) {
    g_autofree gchar *shown = bosonoga_token_shown(name);
    return bosonoga_fail_at(parser, name, error, BOSONOGA_ERROR_NAME,
                            "'%s' ends with " BOSONOGA_FUNCTION_SUFFIX
                            ", which only the name of a function does",
                            shown);
  }
  const gint found = bosonoga_find_function(parser, name);
  if (found >= 0) {
    g_autofree gchar *shown = bosonoga_token_shown(name);
    return bosonoga_fail_at(
        parser, name, error, BOSONOGA_ERROR_NAME, "'%s' is already %s", shown,
        g_array_index(parser->functions, BosonogaFunction, (guint)found)
                .is_array
            ? "an array"
            : "a function");
  }
  return bosonoga_advance(parser, error);
}

gboolean bosonoga_at_value(const BosonogaParser *parser) {
  const BosonogaToken *token = &parser->tokenizer.token;
  switch (token->kind) {
    case BOSONOGA_TOKEN_INTEGER:
    case BOSONOGA_TOKEN_REAL:
    case BOSONOGA_TOKEN_REAL64:
    case BOSONOGA_TOKEN_STRING:
      return TRUE;
    case BOSONOGA_TOKEN_WORD: {
      BosonogaValue value;
      return !bosonoga_token_is_reserved(parser, token) ||
             bosonoga_token_is_numbered(token, BOSONOGA_SHIFT_PREFIX) ||
             bosonoga_token_is_result(token) ||
             bosonoga_token_is_word(token, BOSONOGA_DELTA_TIME_WORD) ||
             bosonoga_token_setting(parser, token, &value);
    }
    case BOSONOGA_TOKEN_END:
    case BOSONOGA_TOKEN_UNSIGNED:
    case BOSONOGA_TOKEN_INDEXED_TYPE:
      return FALSE;
  }
  g_return_val_if_reached(FALSE);
}

gboolean bosonoga_parse_value(BosonogaParser *parser, BosonogaOperand *operand,
                              GError **error) {
  const BosonogaToken *token = &parser->tokenizer.token;
  if (!bosonoga_at_value(parser)) {
    return bosonoga_parse_error(parser, "a value", error);
  }

  memset(operand, 0, sizeof(*operand));
  if (token->kind == BOSONOGA_TOKEN_INTEGER) {
    operand->literal.kind = BOSONOGA_VALUE_I32;
    operand->literal.i32 = token->integer;
  } else if (token->kind == BOSONOGA_TOKEN_REAL) {
    operand->literal.kind = BOSONOGA_VALUE_F32;
    operand->literal.f32 = (gfloat)token->real;
  } else if (token->kind == BOSONOGA_TOKEN_REAL64) {
    operand->literal.kind = BOSONOGA_VALUE_F64;
    operand->literal.f64 = token->real;
  } else if (token->kind == BOSONOGA_TOKEN_STRING) {
    operand->literal.kind = BOSONOGA_VALUE_STR;
    operand->literal.str.text = token->string;
    operand->literal.str.length = token->string_length;
  } else if (bosonoga_token_is_numbered(token, BOSONOGA_SHIFT_PREFIX)) {
    guint64 bit = 0;
    if (!bosonoga_numbered_value(token, BOSONOGA_SHIFT_PREFIX,
                                 BOSONOGA_SHIFT_BIT_MAX, &bit)) {
      g_autofree gchar *shown = bosonoga_token_shown(token);
      return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_SYNTAX,
                              "'%s' is out of range, the last bit is %s%d",
                              shown, BOSONOGA_SHIFT_PREFIX,
                              BOSONOGA_SHIFT_BIT_MAX);
    }
    operand->literal.kind = BOSONOGA_VALUE_FLAGS;
    operand->literal.flags = 1U << bit;
  } else if (bosonoga_token_is_result(token)) {
    if (!bosonoga_result_operand(parser, token, operand, error)) {
      return FALSE;
    }
  } else if (bosonoga_token_is_word(token, BOSONOGA_DELTA_TIME_WORD)) {
    if (parser->section != BOSONOGA_SECTION_GFX_FRAME) {
      return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_SYNTAX,
                              "%s can only be in gfx_frame",
                              BOSONOGA_DELTA_TIME_WORD);
    }
    operand->literal.kind = BOSONOGA_VALUE_F32;
    operand->is_variable = TRUE;
    operand->slot = parser->delta_time_slot;
  } else if (bosonoga_find_constant(parser, token) != NULL) {
    operand->literal = bosonoga_find_constant(parser, token)->value;
  } else if (bosonoga_token_setting(parser, token, &operand->literal)) {
    // The value of a color or global is known now, so it is a literal.
  } else {
    const BosonogaVariable *variable = bosonoga_find_variable(parser, token);
    if (variable == NULL) {
      return bosonoga_fail_unknown_variable(parser, token, error);
    }
    operand->literal.kind = variable->kind;
    operand->is_variable = TRUE;
    operand->slot = variable->slot;
  }
  return bosonoga_advance(parser, error);
}

gboolean bosonoga_parse_value_of_kind(BosonogaParser *parser,
                                      BosonogaValueKind kind,
                                      const gchar *context,
                                      BosonogaOperand *operand,
                                      GError **error) {
  const BosonogaToken token = parser->tokenizer.token;
  if (!bosonoga_parse_value(parser, operand, error)) {
    return FALSE;
  }
  if (operand->literal.kind != kind) {
    g_autofree gchar *shown = bosonoga_token_shown(&token);
    return bosonoga_fail_at(parser, &token, error, BOSONOGA_ERROR_TYPE,
                            "%s needs %s, but '%s' is %s", context,
                            bosonoga_value_kind_name(kind), shown,
                            bosonoga_value_kind_name(operand->literal.kind));
  }
  return TRUE;
}
