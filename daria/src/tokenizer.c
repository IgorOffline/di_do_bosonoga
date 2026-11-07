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

static gboolean daria_at_number(const DariaTokenizer *tokenizer) {
  const gchar *digit =
      *tokenizer->cursor == '-' ? tokenizer->cursor + 1 : tokenizer->cursor;
  return digit < tokenizer->end && g_ascii_isdigit(*digit);
}

static void daria_consume_digits(DariaTokenizer *tokenizer) {
  while (daria_at_digit(tokenizer)) {
    daria_consume_char(tokenizer);
  }
}

static void daria_skip_blanks_and_comments(DariaTokenizer *tokenizer) {
  while (tokenizer->cursor < tokenizer->end) {
    if (g_ascii_isspace(*tokenizer->cursor)) {
      daria_consume_char(tokenizer);
    } else if (tokenizer->end - tokenizer->cursor >= 2 &&
               tokenizer->cursor[0] == '/' && tokenizer->cursor[1] == '/') {
      while (tokenizer->cursor < tokenizer->end && *tokenizer->cursor != '\n') {
        daria_consume_char(tokenizer);
      }
    } else {
      return;
    }
  }
}

// Reads a number, whose type is decided by its suffix, into token.
static gboolean daria_lex_number(DariaTokenizer *tokenizer, DariaToken *token,
                                 GError **error) {
  if (*tokenizer->cursor == '-') {
    daria_consume_char(tokenizer);
  }
  daria_consume_digits(tokenizer);
  if (tokenizer->cursor < tokenizer->end && *tokenizer->cursor == '.') {
    return daria_lex_error(tokenizer, token->line, token->column,
                           "the decimal separator is the letter d, as in "
                           "1d5_f32",
                           error);
  }
  gboolean has_fraction = FALSE;
  if (tokenizer->cursor < tokenizer->end && *tokenizer->cursor == 'd') {
    has_fraction = TRUE;
    daria_consume_char(tokenizer);
    if (!daria_at_digit(tokenizer)) {
      return daria_lex_error(tokenizer, token->line, token->column,
                             "expected a digit after the decimal separator d",
                             error);
    }
    daria_consume_digits(tokenizer);
  }
  g_autofree gchar *number =
      g_strndup(token->text, (gsize)(tokenizer->cursor - token->text));

  const gchar *suffix_start = tokenizer->cursor;
  while (daria_at_word_char(tokenizer)) {
    daria_consume_char(tokenizer);
  }
  g_autofree gchar *suffix =
      g_strndup(suffix_start, (gsize)(tokenizer->cursor - suffix_start));

  if (g_str_equal(suffix, "_i32")) {
    if (has_fraction) {
      return daria_lex_error(tokenizer, token->line, token->column,
                             "an integer cannot have a fraction", error);
    }
    gint64 value = 0;
    if (!g_ascii_string_to_signed(number, 10, G_MININT32, G_MAXINT32, &value,
                                  NULL)) {
      return daria_lex_error(tokenizer, token->line, token->column,
                             "integer does not fit in 32 bits", error);
    }
    token->kind = DARIA_TOKEN_INTEGER;
    token->integer = (gint32)value;
    return TRUE;
  }
  if (g_str_equal(suffix, "_f32")) {
    const gdouble value = g_ascii_strtod(g_strdelimit(number, "d", '.'), NULL);
    if (ABS(value) > (gdouble)G_MAXFLOAT) {
      return daria_lex_error(tokenizer, token->line, token->column,
                             "real number does not fit in 32 bits", error);
    }
    token->kind = DARIA_TOKEN_REAL;
    token->real = (gfloat)value;
    return TRUE;
  }
  return daria_lex_error(tokenizer, token->line, token->column,
                         "number must have the suffix _i32 or _f32", error);
}

void daria_tokenizer_init(DariaTokenizer *tokenizer, const gchar *path,
                          const gchar *source, gsize length) {
  const DariaTokenizer initial = {path, source, source + length, 1, 1, {0}};
  *tokenizer = initial;
}

gboolean daria_tokenizer_advance(DariaTokenizer *tokenizer, GError **error) {
  daria_skip_blanks_and_comments(tokenizer);

  DariaToken token = {DARIA_TOKEN_END,
                      tokenizer->cursor,
                      0,
                      tokenizer->line,
                      tokenizer->column,
                      0,
                      0.0f};
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
    } else if (daria_at_number(tokenizer)) {
      if (!daria_lex_number(tokenizer, &token, error)) {
        return FALSE;
      }
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
