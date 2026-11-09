#include "parser/internal.h"

gboolean bosonoga_parse_if(BosonogaParser *parser, GError **error) {
  g_autoptr(GArray) exit_jumps = g_array_new(FALSE, FALSE, sizeof(guint));
  g_autoptr(GArray) false_jumps = g_array_new(FALSE, FALSE, sizeof(guint));
  gboolean has_condition = TRUE;
  while (has_condition) {
    g_array_set_size(false_jumps, 0);
    if (!bosonoga_parse_condition(parser, false_jumps, error) ||
        !bosonoga_parse_block(parser, error)) {
      return FALSE;
    }
    if (!bosonoga_at_word(parser, "else")) {
      bosonoga_patch_jumps_to_here(parser, false_jumps);
      break;
    }

    const guint jump = parser->ops->len;
    bosonoga_emit(parser, BOSONOGA_OP_JUMP, &parser->tokenizer.token);
    g_array_append_val(exit_jumps, jump);
    bosonoga_patch_jumps_to_here(parser, false_jumps);
    if (!bosonoga_advance(parser, error)) {
      return FALSE;
    }

    has_condition = bosonoga_at_word(parser, "if");
    if (has_condition) {
      if (!bosonoga_advance(parser, error)) {
        return FALSE;
      }
    } else if (!bosonoga_parse_block(parser, error)) {
      return FALSE;
    }
  }
  bosonoga_patch_jumps_to_here(parser, exit_jumps);
  return TRUE;
}

// The body works on a copy of the hidden counter, so setting the variable does
// not change the iterations.
gboolean bosonoga_parse_loop(BosonogaParser *parser, GError **error) {
  const BosonogaToken start = parser->tokenizer.token;
  guint64 limit = 0;
  if (!bosonoga_numbered_value(&start, BOSONOGA_LOOP_PREFIX, G_MAXINT32,
                               &limit)) {
    g_autofree gchar *shown = bosonoga_token_shown(&start);
    return bosonoga_fail_at(parser, &start, error, BOSONOGA_ERROR_SYNTAX,
                            "'%s' does not fit in 32 bits", shown);
  }
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }

  BosonogaToken name;
  const gboolean has_name = bosonoga_at_word(parser, "with");
  if (has_name) {
    if (!bosonoga_advance(parser, error) ||
        !bosonoga_parse_new_name(parser, &name, error)) {
      return FALSE;
    }
  }

  const guint counter = parser->slot_count++;
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_ASSIGN, &start);
  op->slot = counter;
  op->left.literal.kind = BOSONOGA_VALUE_I32;

  const guint test = parser->ops->len;
  op = bosonoga_emit(parser, BOSONOGA_OP_LOOP_TEST, &start);
  op->slot = counter;
  op->integer = (gint32)limit;

  const guint visible_count = parser->variables->len;
  if (has_name) {
    const guint slot = parser->slot_count++;
    op = bosonoga_emit(parser, BOSONOGA_OP_ASSIGN, &start);
    op->slot = slot;
    op->left.literal.kind = BOSONOGA_VALUE_I32;
    op->left.is_variable = TRUE;
    op->left.slot = counter;
    bosonoga_declare_variable(parser, &name, BOSONOGA_VALUE_I32, slot);
  }

  if (!bosonoga_parse_block(parser, error)) {
    return FALSE;
  }
  g_array_set_size(parser->variables, visible_count);

  op = bosonoga_emit(parser, BOSONOGA_OP_LOOP_NEXT, &start);
  op->slot = counter;
  op->target = test;
  g_array_index(parser->ops, BosonogaOp, test).target = parser->ops->len;
  return TRUE;
}
