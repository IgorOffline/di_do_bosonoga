#include "tokenizer.h"

#include "daria.h"

static gboolean daria_lex_error(const DariaTokenizer *tokenizer, gsize line,
                                gsize column, const gchar *message,
                                GError **error) {
  g_set_error(error, DARIA_ERROR, DARIA_ERROR_SYNTAX,
              "%s:%" G_GSIZE_FORMAT ":%" G_GSIZE_FORMAT ": %s", tokenizer->path,
              line, column, message);
  return FALSE;
}

static void daria_consume_char(DariaTokenizer *tokenizer) {
  if (*tokenizer->cursor == '\n') {
    ++tokenizer->line;
    tokenizer->column = 1;
  } else {
    ++tokenizer->column;
  }
  ++tokenizer->cursor;
}

static gboolean daria_at_word_char(const DariaTokenizer *tokenizer) {
  return tokenizer->cursor < tokenizer->end &&
         (g_ascii_isalnum(*tokenizer->cursor) || *tokenizer->cursor == '_');
}

static gboolean daria_at_digit(const DariaTokenizer *tokenizer) {
  return tokenizer->cursor < tokenizer->end &&
         g_ascii_isdigit(*tokenizer->cursor);
}

void daria_tokenizer_init(DariaTokenizer *tokenizer, const gchar *path,
                          const gchar *source, gsize length) {
  const DariaTokenizer initial = {path, source, source + length, 1, 1, {0}};
  *tokenizer = initial;
}

gboolean daria_tokenizer_advance(DariaTokenizer *tokenizer, GError **error) {
  while (tokenizer->cursor < tokenizer->end &&
         g_ascii_isspace(*tokenizer->cursor)) {
    daria_consume_char(tokenizer);
  }

  DariaToken token = {DARIA_TOKEN_END, tokenizer->cursor, 0,
                      tokenizer->line, tokenizer->column, 0};
  if (tokenizer->cursor < tokenizer->end) {
    const gchar first = *tokenizer->cursor;
    if (first == '"') {
      token.kind = DARIA_TOKEN_STRING;
      daria_consume_char(tokenizer);
      while (tokenizer->cursor == tokenizer->end || *tokenizer->cursor != '"') {
        if (tokenizer->cursor == tokenizer->end || *tokenizer->cursor == '\n') {
          return daria_lex_error(tokenizer, token.line, token.column,
                                 "unterminated string", error);
        }
        if (*tokenizer->cursor == '\\') {
          return daria_lex_error(tokenizer, tokenizer->line, tokenizer->column,
                                 "escape sequences are not supported", error);
        }
        if (*tokenizer->cursor == '\0') {
          return daria_lex_error(tokenizer, tokenizer->line, tokenizer->column,
                                 "NUL byte in string", error);
        }
        daria_consume_char(tokenizer);
      }
      daria_consume_char(tokenizer);
    } else if (g_ascii_isdigit(first)) {
      token.kind = DARIA_TOKEN_INTEGER;
      gint64 value = 0;
      while (daria_at_digit(tokenizer)) {
        value = value * 10 + g_ascii_digit_value(*tokenizer->cursor);
        if (value > G_MAXINT32) {
          return daria_lex_error(tokenizer, token.line, token.column,
                                 "integer does not fit in 32 bits", error);
        }
        daria_consume_char(tokenizer);
      }
      if (daria_at_word_char(tokenizer)) {
        return daria_lex_error(tokenizer, token.line, token.column,
                               "invalid integer", error);
      }
      token.integer = (gint32)value;
    } else if (g_ascii_isalpha(first) || first == '_') {
      token.kind = DARIA_TOKEN_WORD;
      while (daria_at_word_char(tokenizer)) {
        daria_consume_char(tokenizer);
      }
    } else {
      return daria_lex_error(tokenizer, token.line, token.column,
                             "unexpected character", error);
    }
    token.length = (gsize)(tokenizer->cursor - token.text);
  }

  tokenizer->token = token;
  return TRUE;
}
