#include "parser/internal.h"

#define BOSONOGA_BLOCK_DEPTH_MAX 256

static gboolean bosonoga_parse_statement(BosonogaParser *parser,
                                         GError **error);

gboolean bosonoga_parse_block(BosonogaParser *parser, GError **error) {
  const BosonogaToken opening = parser->tokenizer.token;
  if (!bosonoga_expect_word(parser, "di", error)) {
    return FALSE;
  }
  if (bosonoga_at_word(parser, "do")) {
    return bosonoga_fail_at(parser, &opening, error, BOSONOGA_ERROR_SYNTAX,
                            "empty block, write nihil for a block that does "
                            "nothing");
  }
  // The parser recurses for each nested block, so the depth must be bounded.
  if (parser->block_depth == BOSONOGA_BLOCK_DEPTH_MAX) {
    return bosonoga_fail_at(parser, &opening, error, BOSONOGA_ERROR_SYNTAX,
                            "blocks are nested more than %d levels deep",
                            BOSONOGA_BLOCK_DEPTH_MAX);
  }

  ++parser->block_depth;
  const guint visible_count = parser->variables->len;
  while (!bosonoga_at_word(parser, "do") &&
         parser->tokenizer.token.kind != BOSONOGA_TOKEN_END) {
    if (!bosonoga_parse_statement(parser, error)) {
      return FALSE;
    }
  }
  for (guint i = visible_count; i < parser->variables->len; ++i) {
    const BosonogaVariable *variable =
        &g_array_index(parser->variables, BosonogaVariable, i);
    if (variable->is_new) {
      return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                              BOSONOGA_ERROR_SYNTAX,
                              "'%.*s' is new, and is unset before the end of "
                              "its block",
                              (gint)variable->length, variable->name);
    }
  }
  // The variables at the top of bosonoga_global are shared by later sections,
  // and so are checked at the end of the program.
  if (parser->section != BOSONOGA_SECTION_GLOBAL || parser->block_depth != 1) {
    if (!bosonoga_check_variables_used(parser, visible_count, error)) {
      return FALSE;
    }
    g_array_set_size(parser->variables, visible_count);
  }
  --parser->block_depth;
  return bosonoga_expect_word(parser, "do", error);
}

static gboolean bosonoga_parse_info(BosonogaParser *parser,
                                    const BosonogaToken *start,
                                    GError **error) {
  if (!bosonoga_at_value(parser)) {
    return bosonoga_parse_error(parser, "a value", error);
  }
  parser->reads_new = TRUE;
  while (bosonoga_at_value(parser)) {
    const BosonogaToken value_token = parser->tokenizer.token;
    BosonogaOperand value;
    if (!bosonoga_parse_value(parser, &value, error)) {
      return FALSE;
    }
    bosonoga_emit(parser, BOSONOGA_OP_PRINT_VALUE, &value_token)->left = value;
  }
  parser->reads_new = FALSE;

  bosonoga_emit(parser, BOSONOGA_OP_PRINT_NEWLINE, start);
  return TRUE;
}

static gboolean bosonoga_parse_sleep(BosonogaParser *parser,
                                     const BosonogaToken *start,
                                     GError **error) {
  BosonogaOperand milliseconds;
  if (!bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_U32, "sleep_ms",
                                    &milliseconds, error)) {
    return FALSE;
  }
  bosonoga_emit(parser, BOSONOGA_OP_SLEEP, start)->left = milliseconds;
  return TRUE;
}

static gboolean bosonoga_parse_await_next_frame(BosonogaParser *parser,
                                                const BosonogaToken *start,
                                                GError **error) {
  if (parser->section != BOSONOGA_SECTION_MAIN || !parser->program->is_gfx ||
      parser->in_function) {
    g_autofree gchar *shown = bosonoga_token_shown(start);
    return bosonoga_fail_at(parser, start, error, BOSONOGA_ERROR_SYNTAX,
                            "%s can only be in gfx_main", shown);
  }
  bosonoga_emit(parser, BOSONOGA_OP_AWAIT_NEXT_FRAME, start);
  return TRUE;
}

static gboolean bosonoga_parse_return(BosonogaParser *parser,
                                      const BosonogaToken *start,
                                      GError **error) {
  if (parser->section != BOSONOGA_SECTION_MAIN) {
    return bosonoga_fail_at(parser, start, error, BOSONOGA_ERROR_SYNTAX,
                            "only bosonoga_main and gfx_main can return");
  }
  gint32 status = BOSONOGA_EXIT_SUCCESS;
  if (bosonoga_at_word(parser, "exit_failure")) {
    status = BOSONOGA_EXIT_FAILURE;
  } else if (!bosonoga_at_word(parser, "exit_success")) {
    return bosonoga_parse_error(parser, "exit_success or exit_failure", error);
  }
  bosonoga_emit(parser, BOSONOGA_OP_RETURN, start)->integer = status;
  return bosonoga_advance(parser, error);
}

static gboolean bosonoga_parse_unset(BosonogaParser *parser, GError **error) {
  const BosonogaToken name = parser->tokenizer.token;
  if (name.kind != BOSONOGA_TOKEN_WORD ||
      bosonoga_token_is_reserved(parser, &name)) {
    return bosonoga_parse_error(parser, "a variable name", error);
  }
  const BosonogaVariable *variable = bosonoga_find_variable(parser, &name);
  if (variable == NULL) {
    return bosonoga_fail_unknown_variable(parser, &name, error);
  }
  g_autofree gchar *shown = bosonoga_token_shown(&name);
  if (!variable->is_new) {
    return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_TYPE,
                            "'%s' is not new, and only what new made is unset",
                            shown);
  }
  if (variable->freed_by != NULL) {
    return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_TYPE,
                            "'%s' is freed after the block that %s gives it, "
                            "and is not unset",
                            shown, variable->freed_by);
  }
  // A visible variable of the same depth is of this block.
  if (variable->block_depth != parser->block_depth) {
    return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_SYNTAX,
                            "'%s' is unset in the block where it is new",
                            shown);
  }
  if (!variable->declaration.is_used) {
    return bosonoga_fail_unused(parser, &variable->declaration, "read", error);
  }
  bosonoga_emit(parser, BOSONOGA_OP_UNSET, &name)->slot = variable->slot;
  g_array_remove_index(parser->variables,
                       (guint)(variable - &g_array_index(parser->variables,
                                                         BosonogaVariable, 0)));
  return bosonoga_advance(parser, error);
}

// After pub, of a variable that later sections share.
static gboolean bosonoga_parse_pub_variable(BosonogaParser *parser,
                                            const BosonogaToken *start,
                                            GError **error) {
  if (parser->scope == 0) {
    return bosonoga_fail_at(parser, start, error, BOSONOGA_ERROR_SYNTAX,
                            "pub is only in a module, a file read with mod");
  }
  if (parser->section != BOSONOGA_SECTION_GLOBAL || parser->block_depth != 1 ||
      parser->in_function) {
    return bosonoga_fail_at(parser, start, error, BOSONOGA_ERROR_SYNTAX,
                            "pub is before a top-level const, def or val, or "
                            "a var or val at the top of bosonoga_global");
  }
  const gboolean is_val = bosonoga_at_word(parser, "val");
  if (!is_val && !bosonoga_at_word(parser, "var")) {
    return bosonoga_parse_error(parser, "var or val after pub", error);
  }
  if (!bosonoga_advance(parser, error) ||
      !bosonoga_parse_var(parser, is_val, error)) {
    return FALSE;
  }
  g_array_index(parser->variables, BosonogaVariable, parser->variables->len - 1)
      .declaration.is_pub = TRUE;
  return TRUE;
}

static gboolean bosonoga_check_in_frame(const BosonogaParser *parser,
                                        const BosonogaToken *start,
                                        GError **error) {
  if (parser->section == BOSONOGA_SECTION_GFX_FRAME) {
    return TRUE;
  }
  g_autofree gchar *shown = bosonoga_token_shown(start);
  return bosonoga_fail_at(parser, start, error, BOSONOGA_ERROR_SYNTAX,
                          "%s can only be in gfx_frame", shown);
}

static gboolean bosonoga_parse_gfx_clear(BosonogaParser *parser,
                                         const BosonogaToken *start,
                                         GError **error) {
  BosonogaOperand color;
  if (!bosonoga_check_in_frame(parser, start, error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_COLOR, "gfx_clear",
                                    &color, error)) {
    return FALSE;
  }
  bosonoga_emit(parser, BOSONOGA_OP_GFX_CLEAR, start)->left = color;
  return TRUE;
}

static gboolean bosonoga_parse_gfx_draw_rectangle(BosonogaParser *parser,
                                                  const BosonogaToken *start,
                                                  GError **error) {
  const gchar *context = "gfx_draw_rectangle";
  BosonogaOperand x;
  BosonogaOperand y;
  BosonogaOperand width;
  BosonogaOperand height;
  BosonogaOperand color;
  if (!bosonoga_check_in_frame(parser, start, error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &x,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &y,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &width,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context,
                                    &height, error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_COLOR, context,
                                    &color, error)) {
    return FALSE;
  }
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_DRAW_RECTANGLE, start);
  op->left = x;
  op->right = y;
  op->third = width;
  op->fourth = height;
  op->fifth = color;
  return TRUE;
}

static gboolean bosonoga_parse_gfx_draw_hex(BosonogaParser *parser,
                                            const BosonogaToken *start,
                                            GError **error) {
  const gchar *context = "gfx_draw_hex";
  BosonogaOperand x;
  BosonogaOperand y;
  BosonogaOperand radius;
  BosonogaOperand color;
  if (!bosonoga_check_in_frame(parser, start, error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &x,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &y,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context,
                                    &radius, error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_COLOR, context,
                                    &color, error)) {
    return FALSE;
  }
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_DRAW_HEX, start);
  op->left = x;
  op->right = y;
  op->third = radius;
  op->fourth = color;
  return TRUE;
}

static gboolean bosonoga_parse_gfx_draw_circle(BosonogaParser *parser,
                                               const BosonogaToken *start,
                                               GError **error) {
  const gchar *context = "gfx_draw_circle";
  BosonogaOperand x;
  BosonogaOperand y;
  BosonogaOperand radius;
  BosonogaOperand color;
  if (!bosonoga_check_in_frame(parser, start, error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &x,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &y,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context,
                                    &radius, error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_COLOR, context,
                                    &color, error)) {
    return FALSE;
  }
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_DRAW_CIRCLE, start);
  op->left = x;
  op->right = y;
  op->third = radius;
  op->fourth = color;
  return TRUE;
}

typedef struct {
  const gchar *word;
  BosonogaGfxAlign align;
} BosonogaAlignWord;

static const BosonogaAlignWord bosonoga_horizontal_aligns[] = {
    {"left", BOSONOGA_GFX_ALIGN_LEFT},
    {"center", BOSONOGA_GFX_ALIGN_CENTER},
    {"right", BOSONOGA_GFX_ALIGN_RIGHT},
};

static const BosonogaAlignWord bosonoga_vertical_aligns[] = {
    {"top", BOSONOGA_GFX_ALIGN_TOP},
    {"middle", BOSONOGA_GFX_ALIGN_MIDDLE},
    {"baseline", BOSONOGA_GFX_ALIGN_BASELINE},
    {"bottom", BOSONOGA_GFX_ALIGN_BOTTOM},
};

static gboolean bosonoga_parse_align(BosonogaParser *parser,
                                     const BosonogaAlignWord *words,
                                     gsize count, const gchar *expected,
                                     gint32 *align, GError **error) {
  for (gsize i = 0; i < count; ++i) {
    if (bosonoga_at_word(parser, words[i].word)) {
      *align |= (gint32)words[i].align;
      return bosonoga_advance(parser, error);
    }
  }
  return bosonoga_parse_error(parser, expected, error);
}

static gboolean bosonoga_parse_gfx_draw_text(BosonogaParser *parser,
                                             const BosonogaToken *start,
                                             GError **error) {
  const gchar *context = "gfx_draw_text";
  gint32 align = 0;
  BosonogaOperand x;
  BosonogaOperand y;
  BosonogaOperand font;
  BosonogaOperand size;
  BosonogaOperand color;
  if (!bosonoga_check_in_frame(parser, start, error) ||
      !bosonoga_parse_align(parser, bosonoga_horizontal_aligns,
                            G_N_ELEMENTS(bosonoga_horizontal_aligns),
                            "left, center or right", &align, error) ||
      !bosonoga_parse_align(parser, bosonoga_vertical_aligns,
                            G_N_ELEMENTS(bosonoga_vertical_aligns),
                            "top, middle, baseline or bottom", &align, error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &x,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &y,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_FONT, context, &font,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &size,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_COLOR, context,
                                    &color, error)) {
    return FALSE;
  }
  if (!bosonoga_at_value(parser)) {
    return bosonoga_parse_error(parser, "a value to draw", error);
  }
  parser->reads_new = TRUE;
  while (bosonoga_at_value(parser)) {
    const BosonogaToken value_token = parser->tokenizer.token;
    BosonogaOperand value;
    if (!bosonoga_parse_value(parser, &value, error)) {
      return FALSE;
    }
    bosonoga_emit(parser, BOSONOGA_OP_DRAW_TEXT_PART, &value_token)->left =
        value;
  }
  parser->reads_new = FALSE;

  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_DRAW_TEXT, start);
  op->integer = align;
  op->left = x;
  op->right = y;
  op->third = font;
  op->fourth = size;
  op->fifth = color;
  return TRUE;
}

static gboolean bosonoga_parse_gfx_draw_icon(BosonogaParser *parser,
                                             const BosonogaToken *start,
                                             GError **error) {
  const gchar *context = "gfx_draw_icon";
  BosonogaOperand x;
  BosonogaOperand y;
  BosonogaOperand icon;
  BosonogaOperand angle;
  BosonogaOperand color;
  if (!bosonoga_check_in_frame(parser, start, error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &x,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &y,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_ICON, context, &icon,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_F32, context, &angle,
                                    error) ||
      !bosonoga_parse_value_of_kind(parser, BOSONOGA_VALUE_COLOR, context,
                                    &color, error)) {
    return FALSE;
  }
  BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_DRAW_ICON, start);
  op->left = x;
  op->right = y;
  op->third = icon;
  op->fourth = angle;
  op->fifth = color;
  return TRUE;
}

static gboolean bosonoga_parse_statement(BosonogaParser *parser,
                                         GError **error) {
  const BosonogaToken start = parser->tokenizer.token;
  if (bosonoga_token_is_numbered(&start, BOSONOGA_LOOP_PREFIX)) {
    return bosonoga_parse_loop(parser, error);
  }
  if (!bosonoga_token_is_word(&start, "info") &&
      !bosonoga_token_is_word(&start, "pub") &&
      !bosonoga_token_is_word(&start, "var") &&
      !bosonoga_token_is_word(&start, "val") &&
      !bosonoga_token_is_word(&start, "set") &&
      !bosonoga_token_is_word(&start, "unset") &&
      !bosonoga_token_is_word(&start, "call") &&
      !bosonoga_token_is_word(&start, "if") &&
      !bosonoga_token_is_word(&start, "while") &&
      !bosonoga_token_is_word(&start, "break") &&
      !bosonoga_token_is_word(&start, "continue") &&
      !bosonoga_token_is_word(&start, "nihil") &&
      !bosonoga_token_is_word(&start, "sleep_ms") &&
      !bosonoga_token_is_word(&start, "gfx_await_next_frame") &&
      !bosonoga_token_is_word(&start, "return") &&
      !bosonoga_token_is_word(&start, "gfx_clear") &&
      !bosonoga_token_is_word(&start, "gfx_draw_rectangle") &&
      !bosonoga_token_is_word(&start, "gfx_draw_hex") &&
      !bosonoga_token_is_word(&start, "gfx_draw_circle") &&
      !bosonoga_token_is_word(&start, "gfx_draw_text") &&
      !bosonoga_token_is_word(&start, "gfx_draw_icon") &&
      !bosonoga_is_given_statement(&start)) {
    return bosonoga_parse_error(parser, "a statement", error);
  }
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }

  if (bosonoga_token_is_word(&start, "info")) {
    return bosonoga_parse_info(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "pub")) {
    return bosonoga_parse_pub_variable(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "var") ||
      bosonoga_token_is_word(&start, "val")) {
    return bosonoga_parse_var(parser, bosonoga_token_is_word(&start, "val"),
                              error);
  }
  if (bosonoga_token_is_word(&start, "set")) {
    return bosonoga_parse_set(parser, error);
  }
  if (bosonoga_token_is_word(&start, "unset")) {
    return bosonoga_parse_unset(parser, error);
  }
  if (bosonoga_token_is_word(&start, "call")) {
    return bosonoga_parse_call_statement(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "if")) {
    return bosonoga_parse_if(parser, error);
  }
  if (bosonoga_token_is_word(&start, "while")) {
    return bosonoga_parse_while(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "break") ||
      bosonoga_token_is_word(&start, "continue")) {
    return bosonoga_parse_loop_jump(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "sleep_ms")) {
    return bosonoga_parse_sleep(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "gfx_await_next_frame")) {
    return bosonoga_parse_await_next_frame(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "return")) {
    const BosonogaVariable *owner = bosonoga_find_new(parser, FALSE);
    if (owner != NULL && owner->freed_by != NULL) {
      return bosonoga_fail_at(parser, &start, error, BOSONOGA_ERROR_SYNTAX,
                              "return cannot leave the block that %s gives "
                              "'%.*s'",
                              owner->freed_by, (gint)owner->length,
                              owner->name);
    }
    if (owner != NULL) {
      return bosonoga_fail_at(parser, &start, error, BOSONOGA_ERROR_SYNTAX,
                              "unset '%.*s' before return", (gint)owner->length,
                              owner->name);
    }
    return parser->in_function
               ? bosonoga_parse_function_return(parser, &start, error)
               : bosonoga_parse_return(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "gfx_clear")) {
    return bosonoga_parse_gfx_clear(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "gfx_draw_rectangle")) {
    return bosonoga_parse_gfx_draw_rectangle(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "gfx_draw_hex")) {
    return bosonoga_parse_gfx_draw_hex(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "gfx_draw_circle")) {
    return bosonoga_parse_gfx_draw_circle(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "gfx_draw_text")) {
    return bosonoga_parse_gfx_draw_text(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "gfx_draw_icon")) {
    return bosonoga_parse_gfx_draw_icon(parser, &start, error);
  }
  if (bosonoga_is_given_statement(&start)) {
    return bosonoga_parse_given_statement(parser, &start, error);
  }
  return TRUE;  // nihil
}
