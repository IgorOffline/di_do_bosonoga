#include "parser/internal.h"

#define DARIA_BLOCK_DEPTH_MAX 256

static gboolean daria_parse_statement(DariaParser *parser, GError **error);

gboolean daria_parse_block(DariaParser *parser, GError **error) {
  const DariaToken opening = parser->tokenizer.token;
  if (!daria_expect_word(parser, "di", error)) {
    return FALSE;
  }
  if (daria_at_word(parser, "do")) {
    return daria_fail_at(parser, &opening, error, DARIA_ERROR_SYNTAX,
                         "empty block, write nihil for a block that does "
                         "nothing");
  }
  // The parser recurses for each nested block, so the depth must be bounded.
  if (parser->block_depth == DARIA_BLOCK_DEPTH_MAX) {
    return daria_fail_at(parser, &opening, error, DARIA_ERROR_SYNTAX,
                         "blocks are nested more than %d levels deep",
                         DARIA_BLOCK_DEPTH_MAX);
  }

  ++parser->block_depth;
  const guint visible_count = parser->variables->len;
  while (!daria_at_word(parser, "do") &&
         parser->tokenizer.token.kind != DARIA_TOKEN_END) {
    if (!daria_parse_statement(parser, error)) {
      return FALSE;
    }
  }
  g_array_set_size(parser->variables, visible_count);
  --parser->block_depth;
  return daria_expect_word(parser, "do", error);
}

// Parses the arguments of an info statement, whose keyword is already consumed.
static gboolean daria_parse_info(DariaParser *parser, const DariaToken *start,
                                 GError **error) {
  const DariaToken *token = &parser->tokenizer.token;
  if (token->kind != DARIA_TOKEN_STRING && !daria_at_value(parser)) {
    return daria_parse_error(parser, "a value or string", error);
  }

  while (token->kind == DARIA_TOKEN_STRING || daria_at_value(parser)) {
    if (token->kind == DARIA_TOKEN_STRING) {
      DariaOp *op = daria_emit(parser, DARIA_OP_PRINT_STRING, token);
      op->text = token->text + 1;
      op->length = token->length - 2;
      if (!daria_advance(parser, error)) {
        return FALSE;
      }
    } else {
      const DariaToken value_token = *token;
      DariaOperand value;
      if (!daria_parse_value(parser, &value, error)) {
        return FALSE;
      }
      daria_emit(parser, DARIA_OP_PRINT_VALUE, &value_token)->left = value;
    }
  }

  daria_emit(parser, DARIA_OP_PRINT_NEWLINE, start);
  return TRUE;
}

// Parses a sleep_ms statement, whose keyword is already consumed.
static gboolean daria_parse_sleep(DariaParser *parser, const DariaToken *start,
                                  GError **error) {
  const DariaToken *token = &parser->tokenizer.token;
  if (token->kind != DARIA_TOKEN_UNSIGNED) {
    return daria_parse_error(parser, "a _u32 number of milliseconds", error);
  }
  daria_emit(parser, DARIA_OP_SLEEP, start)->milliseconds =
      token->unsigned_integer;
  return daria_advance(parser, error);
}

// Parses a return statement, whose keyword is already consumed.
static gboolean daria_parse_return(DariaParser *parser, const DariaToken *start,
                                   GError **error) {
  if (parser->in_callback) {
    return daria_fail_at(parser, start, error, DARIA_ERROR_SYNTAX,
                         "only main and gfx_main can return");
  }
  gint32 status = DARIA_EXIT_SUCCESS;
  if (daria_at_word(parser, "exit_failure")) {
    status = DARIA_EXIT_FAILURE;
  } else if (!daria_at_word(parser, "exit_success")) {
    return daria_parse_error(parser, "exit_success or exit_failure", error);
  }
  daria_emit(parser, DARIA_OP_RETURN, start)->integer = status;
  return daria_advance(parser, error);
}

static gboolean daria_parse_statement(DariaParser *parser, GError **error) {
  const DariaToken start = parser->tokenizer.token;
  if (daria_token_is_numbered(&start, DARIA_LOOP_PREFIX)) {
    return daria_parse_loop(parser, error);
  }
  if (!daria_token_is_word(&start, "info") &&
      !daria_token_is_word(&start, "var") &&
      !daria_token_is_word(&start, "set") &&
      !daria_token_is_word(&start, "if") &&
      !daria_token_is_word(&start, "nihil") &&
      !daria_token_is_word(&start, "sleep_ms") &&
      !daria_token_is_word(&start, "return")) {
    return daria_parse_error(parser, "a statement", error);
  }
  if (!daria_advance(parser, error)) {
    return FALSE;
  }

  if (daria_token_is_word(&start, "info")) {
    return daria_parse_info(parser, &start, error);
  }
  if (daria_token_is_word(&start, "var")) {
    return daria_parse_var(parser, error);
  }
  if (daria_token_is_word(&start, "set")) {
    return daria_parse_set(parser, error);
  }
  if (daria_token_is_word(&start, "if")) {
    return daria_parse_if(parser, error);
  }
  if (daria_token_is_word(&start, "sleep_ms")) {
    return daria_parse_sleep(parser, &start, error);
  }
  if (daria_token_is_word(&start, "return")) {
    return daria_parse_return(parser, &start, error);
  }
  return TRUE;  // nihil
}
