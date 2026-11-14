#include <string.h>

#include "parser/internal.h"
#include "xml.h"

// new xml_str, to_join, and the statements that give values to a block after
// with ret: io_read, io_write, io_modified, xml_child, xml_attribute and
// str_parse.

static gboolean bosonoga_parse_xml_name(BosonogaParser *parser,
                                        const gchar *what,
                                        BosonogaOperand *operand,
                                        GError **error) {
  const BosonogaToken token = parser->tokenizer.token;
  if (token.kind != BOSONOGA_TOKEN_LITERAL ||
      token.value.kind != BOSONOGA_VALUE_STR) {
    g_autofree gchar *expected = g_strdup_printf(
        "the name of %s, a str literal like \"action\"_str", what);
    return bosonoga_parse_error(parser, expected, error);
  }
  if (!bosonoga_xml_is_name(token.value.str.text, token.value.str.length)) {
    g_autofree gchar *shown = bosonoga_token_shown(&token);
    return bosonoga_fail_at(parser, &token, error, BOSONOGA_ERROR_SYNTAX,
                            "%s is not the name of %s, which starts with an "
                            "ASCII letter or _, and has letters, digits, _, - "
                            "and .",
                            shown, what);
  }
  memset(operand, 0, sizeof(*operand));
  operand->literal = token.value;
  return bosonoga_advance(parser, error);
}

static void bosonoga_emit_xml_part(BosonogaParser *parser,
                                   const BosonogaToken *token,
                                   BosonogaXmlPart part,
                                   const BosonogaOperand *value) {
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_XML_PART, token);
  op->integer = (gint32)part;
  op->left = *value;
}

// An xml_str joins as it is, and any other value as text.
static gboolean bosonoga_parse_xml_content(BosonogaParser *parser,
                                           GError **error) {
  if (!bosonoga_at_value(parser)) {
    return bosonoga_parse_error(parser, "a value", error);
  }
  while (bosonoga_at_value(parser)) {
    const BosonogaToken token = parser->tokenizer.token;
    BosonogaOperand value;
    if (!bosonoga_parse_value(parser, &value, error)) {
      return FALSE;
    }
    bosonoga_emit_xml_part(parser, &token,
                           value.literal.kind == BOSONOGA_VALUE_XML_STR
                               ? BOSONOGA_XML_PART_RAW
                               : BOSONOGA_XML_PART_TEXT,
                           &value);
  }
  return TRUE;
}

static gboolean bosonoga_parse_xml_element(BosonogaParser *parser,
                                           GError **error) {
  const BosonogaToken start = parser->tokenizer.token;
  BosonogaOperand element;
  if (!bosonoga_advance(parser, error) ||
      !bosonoga_parse_xml_name(parser, "an element", &element, error)) {
    return FALSE;
  }
  bosonoga_emit_xml_part(parser, &start, BOSONOGA_XML_PART_OPEN, &element);
  g_autoptr(GArray) names = g_array_new(FALSE, FALSE, sizeof(BosonogaToken));
  while (parser->tokenizer.token.kind == BOSONOGA_TOKEN_LITERAL &&
         parser->tokenizer.token.value.kind == BOSONOGA_VALUE_STR) {
    const BosonogaToken name_token = parser->tokenizer.token;
    for (guint i = 0; i < names->len; ++i) {
      const BosonogaToken *seen = &g_array_index(names, BosonogaToken, i);
      if (seen->value.str.length == name_token.value.str.length &&
          memcmp(seen->value.str.text, name_token.value.str.text,
                 seen->value.str.length) == 0) {
        g_autofree gchar *shown = bosonoga_token_shown(&name_token);
        return bosonoga_fail_at(parser, &name_token, error,
                                BOSONOGA_ERROR_SYNTAX,
                                "the attribute %s is already given", shown);
      }
    }
    g_array_append_val(names, name_token);
    BosonogaOperand name;
    if (!bosonoga_parse_xml_name(parser, "an attribute", &name, error)) {
      return FALSE;
    }
    bosonoga_emit_xml_part(parser, &name_token, BOSONOGA_XML_PART_NAME, &name);
    const BosonogaToken value_token = parser->tokenizer.token;
    if (!bosonoga_at_value(parser)) {
      return bosonoga_parse_error(parser, "the value of the attribute", error);
    }
    BosonogaOperand value;
    if (!bosonoga_parse_value(parser, &value, error)) {
      return FALSE;
    }
    bosonoga_emit_xml_part(parser, &value_token, BOSONOGA_XML_PART_VALUE,
                           &value);
  }
  // No statement starts with a value, so this is a name that is not a literal.
  if (bosonoga_at_value(parser) && !bosonoga_at_word(parser, "of")) {
    BosonogaOperand name;
    return bosonoga_parse_xml_name(parser, "an attribute", &name, error);
  }
  BosonogaOperand none;
  memset(&none, 0, sizeof(none));
  if (!bosonoga_at_word(parser, "of")) {
    bosonoga_emit_xml_part(parser, &start, BOSONOGA_XML_PART_EMPTY, &none);
    return TRUE;
  }
  bosonoga_emit_xml_part(parser, &start, BOSONOGA_XML_PART_START, &none);
  if (!bosonoga_advance(parser, error) ||
      !bosonoga_parse_xml_content(parser, error)) {
    return FALSE;
  }
  bosonoga_emit_xml_part(parser, &start, BOSONOGA_XML_PART_CLOSE, &element);
  return TRUE;
}

gboolean bosonoga_parse_new_xml(BosonogaParser *parser,
                                const BosonogaToken *name, gboolean is_val,
                                GError **error) {
  parser->reads_new = TRUE;
  if (bosonoga_at_word(parser, "element")) {
    if (!bosonoga_parse_xml_element(parser, error)) {
      return FALSE;
    }
  } else if (bosonoga_at_word(parser, "of")) {
    if (!bosonoga_advance(parser, error) ||
        !bosonoga_parse_xml_content(parser, error)) {
      return FALSE;
    }
  } else {
    return bosonoga_parse_error(
        parser, "element or of, as in new xml_str element \"action\"_str",
        error);
  }
  parser->reads_new = FALSE;
  const guint slot = parser->slot_count++;
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_NEW_STR, name);
  op->slot = slot;
  op->made = BOSONOGA_VALUE_XML_STR;
  bosonoga_declare_variable(parser, name, BOSONOGA_VALUE_XML_STR, slot);
  BosonogaVariable *variable = &g_array_index(
      parser->variables, BosonogaVariable, parser->variables->len - 1);
  variable->is_val = is_val;
  variable->is_new = TRUE;
  return TRUE;
}

// Joining to an xml_str is not reading it, so it is not used by this.
gboolean bosonoga_parse_join(BosonogaParser *parser, const BosonogaToken *name,
                             const BosonogaVariable *target, GError **error) {
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  BosonogaOperand joined;
  memset(&joined, 0, sizeof(joined));
  joined.literal.kind = BOSONOGA_VALUE_XML_STR;
  joined.is_variable = TRUE;
  joined.slot = target->slot;
  bosonoga_emit_xml_part(parser, name, BOSONOGA_XML_PART_RAW, &joined);
  parser->reads_new = TRUE;
  if (!bosonoga_parse_xml_content(parser, error)) {
    return FALSE;
  }
  parser->reads_new = FALSE;
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_JOIN_STORE, name);
  op->slot = target->slot;
  op->made = BOSONOGA_VALUE_XML_STR;
  return TRUE;
}

typedef enum {
  BOSONOGA_GIVEN_IO_READ,
  BOSONOGA_GIVEN_IO_WRITE,
  BOSONOGA_GIVEN_IO_MODIFIED,
  BOSONOGA_GIVEN_XML_CHILD,
  BOSONOGA_GIVEN_XML_ATTRIBUTE,
  BOSONOGA_GIVEN_STR_PARSE,
} BosonogaGiven;

static const struct {
  const gchar *word;
  BosonogaOpKind op;
} bosonoga_givens[] = {
    [BOSONOGA_GIVEN_IO_READ] = {"io_read", BOSONOGA_OP_IO_READ},
    [BOSONOGA_GIVEN_IO_WRITE] = {"io_write", BOSONOGA_OP_IO_WRITE},
    [BOSONOGA_GIVEN_IO_MODIFIED] = {"io_modified", BOSONOGA_OP_IO_MODIFIED},
    [BOSONOGA_GIVEN_XML_CHILD] = {"xml_child", BOSONOGA_OP_XML_CHILD},
    [BOSONOGA_GIVEN_XML_ATTRIBUTE] = {"xml_attribute",
                                      BOSONOGA_OP_XML_ATTRIBUTE},
    [BOSONOGA_GIVEN_STR_PARSE] = {"str_parse", BOSONOGA_OP_STR_PARSE},
};

gboolean bosonoga_is_given_statement(const BosonogaToken *token) {
  for (gsize i = 0; i < G_N_ELEMENTS(bosonoga_givens); ++i) {
    if (bosonoga_token_is_word(token, bosonoga_givens[i].word)) {
      return TRUE;
    }
  }
  return FALSE;
}

static void bosonoga_add_result(BosonogaResults *results,
                                BosonogaValueKind kind, gboolean is_owned) {
  results->kinds[results->count] = kind;
  results->is_owned[results->count] = is_owned;
  ++results->count;
}

// The values before with ret, and what the statement gives.
static gboolean bosonoga_parse_given_operands(BosonogaParser *parser,
                                              BosonogaGiven given,
                                              BosonogaOp *operands,
                                              BosonogaResults *results,
                                              GError **error) {
  const gchar *word = bosonoga_givens[given].word;
  const BosonogaValueKind bool_kind = BOSONOGA_VALUE_BOOL;
  switch (given) {
    case BOSONOGA_GIVEN_IO_READ:
    case BOSONOGA_GIVEN_IO_WRITE:
    case BOSONOGA_GIVEN_IO_MODIFIED:
      if (!bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_DIR, word,
                                        &operands->left, error) ||
          !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_STR, word,
                                        &operands->right, error)) {
        return FALSE;
      }
      if (given == BOSONOGA_GIVEN_IO_READ) {
        bosonoga_add_result(results, bool_kind, FALSE);
        bosonoga_add_result(results, BOSONOGA_VALUE_XML_STR, TRUE);
        results->also[1] = 1U << BOSONOGA_VALUE_STR;
        bosonoga_add_result(results, BOSONOGA_VALUE_STR, TRUE);
      } else if (given == BOSONOGA_GIVEN_IO_WRITE) {
        const BosonogaToken token = parser->tokenizer.token;
        if (!bosonoga_parse_value(parser, &operands->third, error)) {
          return FALSE;
        }
        if (operands->third.literal.kind != BOSONOGA_VALUE_STR &&
            operands->third.literal.kind != BOSONOGA_VALUE_XML_STR) {
          g_autofree gchar *shown = bosonoga_token_shown(&token);
          return bosonoga_fail_at(
              parser, &token, error, BOSONOGA_ERROR_TYPE,
              "io_write writes a str or an xml_str, but '%s' is %s", shown,
              bosonoga_value_kind_name(operands->third.literal.kind));
        }
        bosonoga_add_result(results, bool_kind, FALSE);
        bosonoga_add_result(results, bool_kind, FALSE);
        bosonoga_add_result(results, BOSONOGA_VALUE_STR, TRUE);
      } else {
        bosonoga_add_result(results, bool_kind, FALSE);
        bosonoga_add_result(results, BOSONOGA_VALUE_I64, FALSE);
      }
      return TRUE;
    case BOSONOGA_GIVEN_XML_CHILD:
    case BOSONOGA_GIVEN_XML_ATTRIBUTE: {
      const gboolean is_child = given == BOSONOGA_GIVEN_XML_CHILD;
      if (!bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_XML_STR, word,
                                        &operands->left, error) ||
          !bosonoga_parse_xml_name(parser,
                                   is_child ? "an element" : "an attribute",
                                   &operands->right, error)) {
        return FALSE;
      }
      if (is_child &&
          !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_I32, word,
                                        &operands->third, error)) {
        return FALSE;
      }
      bosonoga_add_result(results, bool_kind, FALSE);
      bosonoga_add_result(
          results, is_child ? BOSONOGA_VALUE_XML_STR : BOSONOGA_VALUE_STR,
          TRUE);
      return TRUE;
    }
    case BOSONOGA_GIVEN_STR_PARSE:
      if (!bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_STR, word,
                                        &operands->left, error)) {
        return FALSE;
      }
      bosonoga_add_result(results, bool_kind, FALSE);
      bosonoga_add_result(results, BOSONOGA_VALUE_I32, FALSE);
      results->also[1] = (1U << BOSONOGA_VALUE_I64) |
                         (1U << BOSONOGA_VALUE_U32) |
                         (1U << BOSONOGA_VALUE_U64);
      return TRUE;
  }
  g_return_val_if_reached(FALSE);
}

static gboolean bosonoga_fail_without_results(const BosonogaParser *parser,
                                              const BosonogaResults *results,
                                              GError **error) {
  GString *named = g_string_new(NULL);
  for (guint i = 0; i < results->count; ++i) {
    g_string_append_printf(named, "%s%u_%s", i == 0 ? "" : ", ", i,
                           bosonoga_value_kind_name(results->kinds[i]));
  }
  g_autofree gchar *shown = g_string_free(named, FALSE);
  return bosonoga_fail_at(
      parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_SYNTAX,
      "%s gives %s, which with ret takes for a block, each named, as in with "
      "ret 0_bool as done",
      results->name, shown);
}

gboolean bosonoga_parse_given_statement(BosonogaParser *parser,
                                        const BosonogaToken *start,
                                        GError **error) {
  gsize found = 0;
  while (!bosonoga_token_is_word(start, bosonoga_givens[found].word)) {
    ++found;
  }
  const BosonogaGiven given = (BosonogaGiven)found;
  BosonogaResults results;
  memset(&results, 0, sizeof(results));
  results.name = bosonoga_givens[given].word;
  results.gives = "gives";
  BosonogaOp operands;
  memset(&operands, 0, sizeof(operands));
  parser->reads_new = TRUE;
  if (!bosonoga_parse_given_operands(parser, given, &operands, &results,
                                     error)) {
    return FALSE;
  }
  parser->reads_new = FALSE;
  if (!bosonoga_at_word(parser, "with")) {
    return bosonoga_fail_without_results(parser, &results, error);
  }
  if (!bosonoga_advance(parser, error) ||
      !bosonoga_expect_word(parser, "ret", error)) {
    return FALSE;
  }

  const guint op_index = parser->ops->len;
  BosonogaOp *op = bosonoga_emit(parser, bosonoga_givens[given].op, start);
  op->left = operands.left;
  op->right = operands.right;
  op->third = operands.third;
  op->made = results.kinds[1];
  const guint visible_count = parser->variables->len;
  BosonogaTaken taken;
  if (!bosonoga_parse_results(parser, start, &results, &taken, error)) {
    return FALSE;
  }
  op = &g_array_index(parser->ops, BosonogaOp, op_index);
  for (guint i = 0; i < taken.count; ++i) {
    op->integer |= (gint32)(1U << taken.indices[i]);
    if (taken.indices[i] == 1) {
      op->made = taken.kinds[i];
    }
  }
  if (given == BOSONOGA_GIVEN_STR_PARSE && (op->integer & 2) == 0) {
    return bosonoga_fail_at(parser, start, error, BOSONOGA_ERROR_SYNTAX,
                            "str_parse takes its number, whose type it parses, "
                            "as in with ret 0_bool as parsed 1_i32 as number");
  }

  if (!bosonoga_parse_block(parser, error) ||
      !bosonoga_check_variables_used(parser, visible_count, error)) {
    return FALSE;
  }
  for (guint i = 0; i < taken.count; ++i) {
    if (results.is_owned[taken.indices[i]]) {
      bosonoga_emit(parser, BOSONOGA_OP_UNSET, start)->slot = taken.slots[i];
    }
  }
  g_array_set_size(parser->variables, visible_count);
  return TRUE;
}
