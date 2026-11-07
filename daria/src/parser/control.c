#include "parser/internal.h"

gboolean daria_parse_if(DariaParser *parser, GError **error) {
  g_autoptr(GArray) exit_jumps = g_array_new(FALSE, FALSE, sizeof(guint));
  g_autoptr(GArray) false_jumps = g_array_new(FALSE, FALSE, sizeof(guint));
  gboolean has_condition = TRUE;
  while (has_condition) {
    g_array_set_size(false_jumps, 0);
    if (!daria_parse_condition(parser, false_jumps, error) ||
        !daria_parse_block(parser, error)) {
      return FALSE;
    }
    if (!daria_at_word(parser, "else")) {
      daria_patch_jumps_to_here(parser, false_jumps);
      break;
    }

    const guint jump = parser->ops->len;
    daria_emit(parser, DARIA_OP_JUMP, &parser->tokenizer.token);
    g_array_append_val(exit_jumps, jump);
    daria_patch_jumps_to_here(parser, false_jumps);
    if (!daria_advance(parser, error)) {
      return FALSE;
    }

    has_condition = daria_at_word(parser, "if");
    if (has_condition) {
      if (!daria_advance(parser, error)) {
        return FALSE;
      }
    } else if (!daria_parse_block(parser, error)) {
      return FALSE;
    }
  }
  daria_patch_jumps_to_here(parser, exit_jumps);
  return TRUE;
}

// The body works on a copy of the hidden counter, so setting the variable does
// not change the iterations.
gboolean daria_parse_loop(DariaParser *parser, GError **error) {
  const DariaToken start = parser->tokenizer.token;
  guint64 limit = 0;
  if (!daria_numbered_value(&start, DARIA_LOOP_PREFIX, G_MAXINT32, &limit)) {
    g_autofree gchar *shown = daria_token_shown(&start);
    return daria_fail_at(parser, &start, error, DARIA_ERROR_SYNTAX,
                         "'%s' does not fit in 32 bits", shown);
  }
  if (!daria_advance(parser, error)) {
    return FALSE;
  }

  DariaToken name;
  const gboolean has_name = daria_at_word(parser, "with");
  if (has_name) {
    if (!daria_advance(parser, error) ||
        !daria_parse_new_name(parser, &name, error)) {
      return FALSE;
    }
  }

  const guint counter = parser->slot_count++;
  DariaOp *op = daria_emit(parser, DARIA_OP_ASSIGN, &start);
  op->slot = counter;
  op->left.literal.kind = DARIA_VALUE_I32;

  const guint test = parser->ops->len;
  op = daria_emit(parser, DARIA_OP_LOOP_TEST, &start);
  op->slot = counter;
  op->integer = (gint32)limit;

  const guint visible_count = parser->variables->len;
  if (has_name) {
    const guint slot = parser->slot_count++;
    op = daria_emit(parser, DARIA_OP_ASSIGN, &start);
    op->slot = slot;
    op->left.literal.kind = DARIA_VALUE_I32;
    op->left.is_variable = TRUE;
    op->left.slot = counter;
    daria_declare_variable(parser, &name, DARIA_VALUE_I32, slot);
  }

  if (!daria_parse_block(parser, error)) {
    return FALSE;
  }
  g_array_set_size(parser->variables, visible_count);

  op = daria_emit(parser, DARIA_OP_LOOP_NEXT, &start);
  op->slot = counter;
  op->target = test;
  g_array_index(parser->ops, DariaOp, test).target = parser->ops->len;
  return TRUE;
}
