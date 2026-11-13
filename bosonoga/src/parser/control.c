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

void bosonoga_loop_clear(gpointer loop) {
  BosonogaLoop *cleared = loop;
  g_array_unref(cleared->break_jumps);
  g_array_unref(cleared->continue_jumps);
}

static void bosonoga_begin_loop(BosonogaParser *parser) {
  const BosonogaLoop loop = {
      g_array_new(FALSE, FALSE, sizeof(guint)),
      g_array_new(FALSE, FALSE, sizeof(guint)),
  };
  g_array_append_val(parser->loops, loop);
}

static void bosonoga_end_loop(BosonogaParser *parser, guint continue_target) {
  const BosonogaLoop *loop =
      &g_array_index(parser->loops, BosonogaLoop, parser->loops->len - 1);
  bosonoga_patch_jumps(parser, loop->continue_jumps, continue_target);
  bosonoga_patch_jumps_to_here(parser, loop->break_jumps);
  g_array_set_size(parser->loops, parser->loops->len - 1);
}

gboolean bosonoga_parse_loop_jump(BosonogaParser *parser,
                                  const BosonogaToken *start, GError **error) {
  if (parser->loops->len == 0) {
    g_autofree gchar *shown = bosonoga_token_shown(start);
    return bosonoga_fail_at(parser, start, error, BOSONOGA_ERROR_SYNTAX,
                            "%s can only be in a loop", shown);
  }
  const BosonogaVariable *owner = bosonoga_find_new(parser, TRUE);
  if (owner != NULL) {
    g_autofree gchar *shown = bosonoga_token_shown(start);
    return bosonoga_fail_at(parser, start, error, BOSONOGA_ERROR_SYNTAX,
                            "unset '%.*s' before %s", (gint)owner->length,
                            owner->name, shown);
  }
  const BosonogaLoop *loop =
      &g_array_index(parser->loops, BosonogaLoop, parser->loops->len - 1);
  const guint jump = parser->ops->len;
  bosonoga_emit(parser, BOSONOGA_OP_JUMP, start);
  g_array_append_val(bosonoga_token_is_word(start, "break")
                         ? loop->break_jumps
                         : loop->continue_jumps,
                     jump);
  return TRUE;
}

gboolean bosonoga_parse_while(BosonogaParser *parser,
                              const BosonogaToken *start, GError **error) {
  const guint condition = parser->ops->len;
  g_autoptr(GArray) false_jumps = g_array_new(FALSE, FALSE, sizeof(guint));
  if (!bosonoga_parse_condition(parser, false_jumps, error)) {
    return FALSE;
  }
  bosonoga_begin_loop(parser);
  if (!bosonoga_parse_block(parser, error)) {
    return FALSE;
  }
  bosonoga_emit(parser, BOSONOGA_OP_JUMP, start)->target = condition;
  bosonoga_end_loop(parser, condition);
  bosonoga_patch_jumps_to_here(parser, false_jumps);
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

  bosonoga_begin_loop(parser);
  if (!bosonoga_parse_block(parser, error) ||
      !bosonoga_check_variables_used(parser, visible_count, error)) {
    return FALSE;
  }
  g_array_set_size(parser->variables, visible_count);

  const guint next = parser->ops->len;
  op = bosonoga_emit(parser, BOSONOGA_OP_LOOP_NEXT, &start);
  op->slot = counter;
  op->target = test;
  g_array_index(parser->ops, BosonogaOp, test).target = parser->ops->len;
  bosonoga_end_loop(parser, next);
  return TRUE;
}
