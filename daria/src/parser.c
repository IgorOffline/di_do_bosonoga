#include "parser.h"

#include "daria.h"
#include "tokenizer.h"

#define DARIA_MESSAGE_TOKEN_MAX 32

static gboolean daria_token_is_word(const DariaToken *token,
                                    const gchar *word) {
  if (token->kind != DARIA_TOKEN_WORD) {
    return FALSE;
  }
  g_autofree gchar *text = g_strndup(token->text, token->length);
  return g_str_equal(text, word);
}

static gboolean daria_parse_error(const DariaTokenizer *tokenizer,
                                  const gchar *expected, GError **error) {
  const DariaToken *token = &tokenizer->token;
  // The source is arbitrary bytes and the cut can split a character, while
  // GError messages must be UTF-8.
  g_autofree gchar *shown = g_utf8_make_valid(
      token->text, (gssize)MIN(token->length, DARIA_MESSAGE_TOKEN_MAX));
  g_autofree gchar *got = token->kind == DARIA_TOKEN_END
                              ? g_strdup("end of file")
                              : g_strdup_printf("'%s'", shown);
  g_set_error(error, DARIA_ERROR, DARIA_ERROR_SYNTAX,
              "%s:%" G_GSIZE_FORMAT ":%" G_GSIZE_FORMAT ": expected %s, got %s",
              tokenizer->path, token->line, token->column, expected, got);
  return FALSE;
}

static gboolean daria_expect_word(DariaTokenizer *tokenizer, const gchar *word,
                                  GError **error) {
  if (!daria_token_is_word(&tokenizer->token, word)) {
    return daria_parse_error(tokenizer, word, error);
  }
  return daria_tokenizer_advance(tokenizer, error);
}

// Parses the arguments of an info statement, whose keyword is already consumed.
static gboolean daria_parse_info(DariaTokenizer *tokenizer, GArray *program,
                                 GError **error) {
  if (tokenizer->token.kind != DARIA_TOKEN_INTEGER &&
      tokenizer->token.kind != DARIA_TOKEN_STRING) {
    return daria_parse_error(tokenizer, "integer or string", error);
  }

  while (tokenizer->token.kind == DARIA_TOKEN_INTEGER ||
         tokenizer->token.kind == DARIA_TOKEN_STRING) {
    const DariaToken *token = &tokenizer->token;
    const DariaOp op =
        token->kind == DARIA_TOKEN_INTEGER
            ? (DariaOp){DARIA_OP_PRINT_INTEGER, token->integer, NULL, 0}
            : (DariaOp){DARIA_OP_PRINT_STRING, 0, token->text + 1,
                        token->length - 2};
    g_array_append_val(program, op);
    if (!daria_tokenizer_advance(tokenizer, error)) {
      return FALSE;
    }
  }

  const DariaOp newline = {DARIA_OP_PRINT_NEWLINE, 0, NULL, 0};
  g_array_append_val(program, newline);
  return TRUE;
}

GArray *daria_parse(const gchar *path, const gchar *source, gsize length,
                    GError **error) {
  g_autoptr(GArray) program = g_array_new(FALSE, FALSE, sizeof(DariaOp));
  DariaTokenizer tokenizer;
  daria_tokenizer_init(&tokenizer, path, source, length);
  if (!daria_tokenizer_advance(&tokenizer, error) ||
      !daria_expect_word(&tokenizer, "main", error) ||
      !daria_expect_word(&tokenizer, "di", error)) {
    return NULL;
  }

  while (daria_token_is_word(&tokenizer.token, "info")) {
    if (!daria_tokenizer_advance(&tokenizer, error) ||
        !daria_parse_info(&tokenizer, program, error)) {
      return NULL;
    }
  }

  if (!daria_expect_word(&tokenizer, "return", error) ||
      !daria_expect_word(&tokenizer, "exit_success", error) ||
      !daria_expect_word(&tokenizer, "do", error)) {
    return NULL;
  }
  if (tokenizer.token.kind != DARIA_TOKEN_END) {
    daria_parse_error(&tokenizer, "end of file", error);
    return NULL;
  }
  return g_steal_pointer(&program);
}
