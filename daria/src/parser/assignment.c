#include <string.h>

#include "parser/internal.h"

// Parses a conversion and stores its result in the slot. The keyword is the
// current token, and the kind of the result is returned in kind.
static gboolean daria_parse_conversion(DariaParser *parser, guint slot,
                                       DariaValueKind *kind, GError **error) {
  const DariaToken start = parser->tokenizer.token;
  const gboolean to_f32 = daria_at_word(parser, "to_f32");
  DariaOperand source;
  if (!daria_advance(parser, error) ||
      !daria_parse_value_of_kind(
          parser, to_f32 ? DARIA_VALUE_I32 : DARIA_VALUE_F32,
          to_f32 ? "to_f32" : "to_i32", &source, error)) {
    return FALSE;
  }
  DariaOp *op = daria_emit(
      parser, to_f32 ? DARIA_OP_CONVERT_TO_F32 : DARIA_OP_CONVERT_TO_I32,
      &start);
  op->slot = slot;
  op->left = source;
  *kind = to_f32 ? DARIA_VALUE_F32 : DARIA_VALUE_I32;
  return TRUE;
}

static gboolean daria_at_conversion(const DariaParser *parser) {
  return daria_at_word(parser, "to_f32") || daria_at_word(parser, "to_i32");
}

gboolean daria_parse_var(DariaParser *parser, GError **error) {
  DariaToken name;
  if (!daria_parse_new_name(parser, &name, error) ||
      !daria_expect_word(parser, "is", error)) {
    return FALSE;
  }

  const guint slot = parser->slot_count++;
  DariaValueKind kind = DARIA_VALUE_I32;
  if (daria_at_conversion(parser)) {
    if (!daria_parse_conversion(parser, slot, &kind, error)) {
      return FALSE;
    }
  } else {
    const DariaToken start = parser->tokenizer.token;
    DariaOperand initial;
    if (!daria_parse_value(parser, &initial, error)) {
      return FALSE;
    }
    kind = initial.literal.kind;
    DariaOp *op = daria_emit(parser, DARIA_OP_ASSIGN, &start);
    op->slot = slot;
    op->left = initial;

    while (kind == DARIA_VALUE_FLAGS && daria_at_word(parser, "shift_or")) {
      const DariaToken shift_or = parser->tokenizer.token;
      DariaOperand flags;
      if (!daria_advance(parser, error) ||
          !daria_parse_value_of_kind(parser, DARIA_VALUE_FLAGS, "shift_or",
                                     &flags, error)) {
        return FALSE;
      }
      op = daria_emit(parser, DARIA_OP_FLAGS_OR, &shift_or);
      op->slot = slot;
      op->left = flags;
    }
  }

  daria_declare_variable(parser, &name, kind, slot);
  return TRUE;
}

// Parses the values of to_add or to_subtract, whose keyword is
// already consumed.
static gboolean daria_parse_accumulation(DariaParser *parser,
                                         const DariaVariable *target,
                                         const DariaToken *start,
                                         gboolean subtract, GError **error) {
  const gchar *context = subtract ? "to_subtract" : "to_add";
  const DariaOpKind step =
      subtract ? DARIA_OP_ACCUMULATOR_SUBTRACT : DARIA_OP_ACCUMULATOR_ADD;
  const DariaValueKind kind = target->kind;
  const guint slot = target->slot;

  DariaOperand first;
  if (!daria_parse_value_of_kind(parser, kind, context, &first, error)) {
    return FALSE;
  }
  if (subtract && !daria_at_value(parser)) {
    DariaOperand zero;
    memset(&zero, 0, sizeof(zero));
    zero.literal.kind = kind;
    daria_emit(parser, DARIA_OP_ACCUMULATOR_LOAD, start)->left = zero;
    daria_emit(parser, step, start)->left = first;
  } else {
    daria_emit(parser, DARIA_OP_ACCUMULATOR_LOAD, start)->left = first;
  }

  while (daria_at_value(parser)) {
    DariaOperand next;
    if (!daria_parse_value_of_kind(parser, kind, context, &next, error)) {
      return FALSE;
    }
    daria_emit(parser, step, start)->left = next;
  }
  daria_emit(parser, DARIA_OP_ACCUMULATOR_STORE, start)->slot = slot;
  return TRUE;
}

gboolean daria_parse_set(DariaParser *parser, GError **error) {
  const DariaToken name = parser->tokenizer.token;
  if (name.kind != DARIA_TOKEN_WORD || daria_token_is_reserved(&name)) {
    return daria_parse_error(parser, "a variable name", error);
  }
  g_autofree gchar *shown = daria_token_shown(&name);
  const DariaVariable *found = daria_find_variable(parser, &name);
  if (found == NULL) {
    return daria_fail_at(parser, &name, error, DARIA_ERROR_NAME,
                         "unknown variable '%s'", shown);
  }
  const DariaVariable target = *found;
  if (!daria_advance(parser, error)) {
    return FALSE;
  }

  const DariaToken operation = parser->tokenizer.token;
  if (daria_at_conversion(parser)) {
    DariaValueKind kind = DARIA_VALUE_I32;
    if (!daria_parse_conversion(parser, target.slot, &kind, error)) {
      return FALSE;
    }
    if (kind != target.kind) {
      return daria_fail_at(parser, &operation, error, DARIA_ERROR_TYPE,
                           "'%s' is %s, but the conversion gives %s", shown,
                           daria_value_kind_name(target.kind),
                           daria_value_kind_name(kind));
    }
    return TRUE;
  }

  const gboolean subtract = daria_at_word(parser, "to_subtract");
  if (!subtract && !daria_at_word(parser, "to_add")) {
    return daria_parse_error(parser, "to_add, to_subtract, to_f32 or to_i32",
                             error);
  }
  if (target.kind == DARIA_VALUE_FLAGS || target.kind == DARIA_VALUE_COLOR) {
    return daria_fail_at(parser, &operation, error, DARIA_ERROR_TYPE,
                         "'%s' is %s, which cannot be added or subtracted",
                         shown, daria_value_kind_name(target.kind));
  }
  return daria_advance(parser, error) &&
         daria_parse_accumulation(parser, &target, &name, subtract, error);
}
