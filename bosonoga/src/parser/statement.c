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
  // The variables at the top of gfx_global stay visible in the sections after
  // it, which share them.
  if (parser->section != BOSONOGA_SECTION_GFX_GLOBAL ||
      parser->block_depth != 1) {
    g_array_set_size(parser->variables, visible_count);
  }
  --parser->block_depth;
  return bosonoga_expect_word(parser, "do", error);
}

// Parses the arguments of an info statement, whose keyword is already consumed.
static gboolean bosonoga_parse_info(BosonogaParser *parser,
                                    const BosonogaToken *start,
                                    GError **error) {
  const BosonogaToken *token = &parser->tokenizer.token;
  if (token->kind != BOSONOGA_TOKEN_STRING && !bosonoga_at_value(parser)) {
    return bosonoga_parse_error(parser, "a value or string", error);
  }

  while (token->kind == BOSONOGA_TOKEN_STRING || bosonoga_at_value(parser)) {
    if (token->kind == BOSONOGA_TOKEN_STRING) {
      BosonogaOp *op = bosonoga_emit(parser, BOSONOGA_OP_PRINT_STRING, token);
      op->text = token->text + 1;
      op->length = token->length - 2;
      if (!bosonoga_advance(parser, error)) {
        return FALSE;
      }
    } else {
      const BosonogaToken value_token = *token;
      BosonogaOperand value;
      if (!bosonoga_parse_value(parser, &value, error)) {
        return FALSE;
      }
      bosonoga_emit(parser, BOSONOGA_OP_PRINT_VALUE, &value_token)->left =
          value;
    }
  }

  bosonoga_emit(parser, BOSONOGA_OP_PRINT_NEWLINE, start);
  return TRUE;
}

// Parses a sleep_ms statement, whose keyword is already consumed.
static gboolean bosonoga_parse_sleep(BosonogaParser *parser,
                                     const BosonogaToken *start,
                                     GError **error) {
  const BosonogaToken *token = &parser->tokenizer.token;
  if (token->kind != BOSONOGA_TOKEN_UNSIGNED) {
    return bosonoga_parse_error(parser, "a _u32 number of milliseconds", error);
  }
  bosonoga_emit(parser, BOSONOGA_OP_SLEEP, start)->milliseconds =
      token->unsigned_integer;
  return bosonoga_advance(parser, error);
}

// Parses a return statement, whose keyword is already consumed.
static gboolean bosonoga_parse_return(BosonogaParser *parser,
                                      const BosonogaToken *start,
                                      GError **error) {
  if (parser->section != BOSONOGA_SECTION_MAIN) {
    return bosonoga_fail_at(parser, start, error, BOSONOGA_ERROR_SYNTAX,
                            "only main and gfx_main can return");
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

// Checks that the statement, located at the token, is in gfx_frame, the only
// section that can have it.
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

// Parses a gfx_clear statement, whose keyword is already consumed.
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

// Parses a draw_rectangle statement, whose keyword is already consumed.
static gboolean bosonoga_parse_draw_rectangle(BosonogaParser *parser,
                                              const BosonogaToken *start,
                                              GError **error) {
  const gchar *context = "draw_rectangle";
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

static gboolean bosonoga_parse_statement(BosonogaParser *parser,
                                         GError **error) {
  const BosonogaToken start = parser->tokenizer.token;
  if (bosonoga_token_is_numbered(&start, BOSONOGA_LOOP_PREFIX)) {
    return bosonoga_parse_loop(parser, error);
  }
  if (!bosonoga_token_is_word(&start, "info") &&
      !bosonoga_token_is_word(&start, "var") &&
      !bosonoga_token_is_word(&start, "set") &&
      !bosonoga_token_is_word(&start, "if") &&
      !bosonoga_token_is_word(&start, "nihil") &&
      !bosonoga_token_is_word(&start, "sleep_ms") &&
      !bosonoga_token_is_word(&start, "return") &&
      !bosonoga_token_is_word(&start, "gfx_clear") &&
      !bosonoga_token_is_word(&start, "draw_rectangle")) {
    return bosonoga_parse_error(parser, "a statement", error);
  }
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }

  if (bosonoga_token_is_word(&start, "info")) {
    return bosonoga_parse_info(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "var")) {
    return bosonoga_parse_var(parser, error);
  }
  if (bosonoga_token_is_word(&start, "set")) {
    return bosonoga_parse_set(parser, error);
  }
  if (bosonoga_token_is_word(&start, "if")) {
    return bosonoga_parse_if(parser, error);
  }
  if (bosonoga_token_is_word(&start, "sleep_ms")) {
    return bosonoga_parse_sleep(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "return")) {
    return bosonoga_parse_return(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "gfx_clear")) {
    return bosonoga_parse_gfx_clear(parser, &start, error);
  }
  if (bosonoga_token_is_word(&start, "draw_rectangle")) {
    return bosonoga_parse_draw_rectangle(parser, &start, error);
  }
  return TRUE;  // nihil
}
