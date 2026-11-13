#include <string.h>

#include "parser/internal.h"

const BosonogaConstant *bosonoga_find_constant_any(const BosonogaParser *parser,
                                                   const BosonogaToken *token) {
  for (guint i = 0; i < parser->constants->len; ++i) {
    const BosonogaConstant *constant =
        &g_array_index(parser->constants, BosonogaConstant, i);
    if (token->kind == BOSONOGA_TOKEN_WORD &&
        token->length == constant->length &&
        memcmp(token->text, constant->name, token->length) == 0) {
      return constant;
    }
  }
  return NULL;
}

const BosonogaConstant *bosonoga_find_constant(const BosonogaParser *parser,
                                               const BosonogaToken *token) {
  const BosonogaConstant *constant = bosonoga_find_constant_any(parser, token);
  return constant != NULL && bosonoga_is_visible(parser, &constant->declaration)
             ? constant
             : NULL;
}

void bosonoga_use_constant(BosonogaParser *parser,
                           const BosonogaConstant *constant) {
  const guint index = (guint)(constant - &g_array_index(parser->constants,
                                                        BosonogaConstant, 0));
  g_array_index(parser->constants, BosonogaConstant, index)
      .declaration.is_used = TRUE;
}

gboolean bosonoga_parse_constant(BosonogaParser *parser, GError **error) {
  BosonogaToken name;
  if (!bosonoga_expect_word(parser, "const", error) ||
      !bosonoga_parse_new_name(parser, &name, error) ||
      !bosonoga_expect_word(parser, "is", error)) {
    return FALSE;
  }
  const BosonogaToken source = parser->tokenizer.token;
  BosonogaOperand value;
  memset(&value, 0, sizeof(value));
  if (bosonoga_at_len(parser)) {
    if (!bosonoga_parse_len(parser, &value.literal, error)) {
      return FALSE;
    }
  } else if (!bosonoga_parse_value(parser, &value, error)) {
    return FALSE;
  }
  if (value.is_variable) {
    return bosonoga_fail_at(
        parser, &source, error, BOSONOGA_ERROR_TYPE,
        "a constant needs a literal or an earlier constant");
  }
  BosonogaConstant constant = {name.text, name.length, value.literal, {0}};
  bosonoga_declare_name(parser, &constant.declaration, &name);
  g_array_append_val(parser->constants, constant);
  return TRUE;
}
