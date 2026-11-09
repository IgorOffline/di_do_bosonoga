#include <string.h>

#include "parser/internal.h"

const BosonogaConstant *bosonoga_find_constant(const BosonogaParser *parser,
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

gboolean bosonoga_parse_constant(BosonogaParser *parser, GError **error) {
  BosonogaToken name;
  if (!bosonoga_expect_word(parser, "const", error) ||
      !bosonoga_parse_new_name(parser, &name, error) ||
      !bosonoga_expect_word(parser, "is", error)) {
    return FALSE;
  }
  const BosonogaToken source = parser->tokenizer.token;
  BosonogaOperand value;
  if (!bosonoga_parse_value(parser, &value, error)) {
    return FALSE;
  }
  if (value.is_variable) {
    return bosonoga_fail_at(
        parser, &source, error, BOSONOGA_ERROR_TYPE,
        "a constant needs a literal or an earlier constant");
  }
  const BosonogaConstant constant = {name.text, name.length, value.literal};
  g_array_append_val(parser->constants, constant);
  return TRUE;
}
