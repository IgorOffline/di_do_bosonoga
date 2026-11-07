#include <string.h>

#include "parser/internal.h"

#define DARIA_SHIFT_BIT_MAX 31

const DariaVariable *daria_find_variable(const DariaParser *parser,
                                         const DariaToken *token) {
  for (guint i = 0; i < parser->variables->len; ++i) {
    const DariaVariable *variable =
        &g_array_index(parser->variables, DariaVariable, i);
    if (variable->length == token->length &&
        memcmp(variable->name, token->text, token->length) == 0) {
      return variable;
    }
  }
  return NULL;
}

void daria_declare_variable(DariaParser *parser, const DariaToken *name,
                            DariaValueKind kind, guint slot) {
  const DariaVariable variable = {name->text, name->length, kind, slot};
  g_array_append_val(parser->variables, variable);
}

gboolean daria_parse_new_name(DariaParser *parser, DariaToken *name,
                              GError **error) {
  *name = parser->tokenizer.token;
  if (name->kind != DARIA_TOKEN_WORD || daria_token_is_reserved(parser, name)) {
    return daria_parse_error(parser, "a variable name", error);
  }
  if (daria_find_variable(parser, name) != NULL) {
    g_autofree gchar *shown = daria_token_shown(name);
    return daria_fail_at(parser, name, error, DARIA_ERROR_NAME,
                         "variable '%s' already exists", shown);
  }
  return daria_advance(parser, error);
}

gboolean daria_at_value(const DariaParser *parser) {
  const DariaToken *token = &parser->tokenizer.token;
  switch (token->kind) {
    case DARIA_TOKEN_INTEGER:
    case DARIA_TOKEN_REAL:
      return TRUE;
    case DARIA_TOKEN_WORD: {
      guint32 color = 0;
      return !daria_token_is_reserved(parser, token) ||
             daria_token_is_numbered(token, DARIA_SHIFT_PREFIX) ||
             daria_token_color(parser, token, &color);
    }
    case DARIA_TOKEN_END:
    case DARIA_TOKEN_UNSIGNED:
    case DARIA_TOKEN_STRING:
      return FALSE;
  }
  g_return_val_if_reached(FALSE);
}

gboolean daria_parse_value(DariaParser *parser, DariaOperand *operand,
                           GError **error) {
  const DariaToken *token = &parser->tokenizer.token;
  if (!daria_at_value(parser)) {
    return daria_parse_error(parser, "a value", error);
  }

  memset(operand, 0, sizeof(*operand));
  guint32 color = 0;
  if (token->kind == DARIA_TOKEN_INTEGER) {
    operand->literal.kind = DARIA_VALUE_I32;
    operand->literal.i32 = token->integer;
  } else if (token->kind == DARIA_TOKEN_REAL) {
    operand->literal.kind = DARIA_VALUE_F32;
    operand->literal.f32 = token->real;
  } else if (daria_token_is_numbered(token, DARIA_SHIFT_PREFIX)) {
    guint64 bit = 0;
    if (!daria_numbered_value(token, DARIA_SHIFT_PREFIX, DARIA_SHIFT_BIT_MAX,
                              &bit)) {
      g_autofree gchar *shown = daria_token_shown(token);
      return daria_fail_at(parser, token, error, DARIA_ERROR_SYNTAX,
                           "'%s' is out of range, the last bit is %s%d", shown,
                           DARIA_SHIFT_PREFIX, DARIA_SHIFT_BIT_MAX);
    }
    operand->literal.kind = DARIA_VALUE_FLAGS;
    operand->literal.flags = 1U << bit;
  } else if (daria_token_color(parser, token, &color)) {
    operand->literal.kind = DARIA_VALUE_COLOR;
    operand->literal.color = color;
  } else {
    const DariaVariable *variable = daria_find_variable(parser, token);
    if (variable == NULL) {
      g_autofree gchar *shown = daria_token_shown(token);
      return daria_fail_at(parser, token, error, DARIA_ERROR_NAME,
                           "unknown variable '%s'", shown);
    }
    operand->literal.kind = variable->kind;
    operand->is_variable = TRUE;
    operand->slot = variable->slot;
  }
  return daria_advance(parser, error);
}

gboolean daria_parse_value_of_kind(DariaParser *parser, DariaValueKind kind,
                                   const gchar *context, DariaOperand *operand,
                                   GError **error) {
  const DariaToken token = parser->tokenizer.token;
  if (!daria_parse_value(parser, operand, error)) {
    return FALSE;
  }
  if (operand->literal.kind != kind) {
    g_autofree gchar *shown = daria_token_shown(&token);
    return daria_fail_at(parser, &token, error, DARIA_ERROR_TYPE,
                         "%s needs %s, but '%s' is %s", context,
                         daria_value_kind_name(kind), shown,
                         daria_value_kind_name(operand->literal.kind));
  }
  return TRUE;
}
