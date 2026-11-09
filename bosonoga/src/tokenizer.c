#include "tokenizer.h"

#include "bosonoga/bosonoga.h"

static gboolean bosonoga_lex_error(const BosonogaTokenizer *tokenizer,
                                   gsize line, gsize column,
                                   const gchar *message, GError **error) {
  g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_SYNTAX,
              "%s:%" G_GSIZE_FORMAT ":%" G_GSIZE_FORMAT ": %s", tokenizer->path,
              line, column, message);
  return FALSE;
}

static void bosonoga_consume_char(BosonogaTokenizer *tokenizer) {
  if (*tokenizer->cursor == '\n') {
    ++tokenizer->line;
    tokenizer->column = 1;
  } else {
    ++tokenizer->column;
  }
  ++tokenizer->cursor;
}

static gboolean bosonoga_at_word_char(const BosonogaTokenizer *tokenizer) {
  return tokenizer->cursor < tokenizer->end &&
         (g_ascii_isalnum(*tokenizer->cursor) || *tokenizer->cursor == '_');
}

static gboolean bosonoga_at_digit(const BosonogaTokenizer *tokenizer) {
  return tokenizer->cursor < tokenizer->end &&
         g_ascii_isdigit(*tokenizer->cursor);
}

static gboolean bosonoga_at_number(const BosonogaTokenizer *tokenizer) {
  const gchar *digit =
      *tokenizer->cursor == '-' ? tokenizer->cursor + 1 : tokenizer->cursor;
  return digit < tokenizer->end && g_ascii_isdigit(*digit);
}

static void bosonoga_consume_digits(BosonogaTokenizer *tokenizer) {
  while (bosonoga_at_digit(tokenizer)) {
    bosonoga_consume_char(tokenizer);
  }
}

static void bosonoga_skip_blanks_and_comments(BosonogaTokenizer *tokenizer) {
  while (tokenizer->cursor < tokenizer->end) {
    if (g_ascii_isspace(*tokenizer->cursor)) {
      bosonoga_consume_char(tokenizer);
    } else if (tokenizer->end - tokenizer->cursor >= 2 &&
               tokenizer->cursor[0] == '/' && tokenizer->cursor[1] == '/') {
      while (tokenizer->cursor < tokenizer->end && *tokenizer->cursor != '\n') {
        bosonoga_consume_char(tokenizer);
      }
    } else {
      return;
    }
  }
}

// Reads a number, whose type is decided by its suffix, into token.
static gboolean bosonoga_lex_number(BosonogaTokenizer *tokenizer,
                                    BosonogaToken *token, GError **error) {
  if (*tokenizer->cursor == '-') {
    bosonoga_consume_char(tokenizer);
  }
  bosonoga_consume_digits(tokenizer);
  if (tokenizer->cursor < tokenizer->end && *tokenizer->cursor == '.') {
    return bosonoga_lex_error(tokenizer, token->line, token->column,
                              "the decimal separator is the letter d, as in "
                              "1d5_f32",
                              error);
  }
  gboolean has_fraction = FALSE;
  if (tokenizer->cursor < tokenizer->end && *tokenizer->cursor == 'd') {
    has_fraction = TRUE;
    bosonoga_consume_char(tokenizer);
    if (!bosonoga_at_digit(tokenizer)) {
      return bosonoga_lex_error(
          tokenizer, token->line, token->column,
          "expected a digit after the decimal separator d", error);
    }
    bosonoga_consume_digits(tokenizer);
  }
  g_autofree gchar *number =
      g_strndup(token->text, (gsize)(tokenizer->cursor - token->text));

  const gchar *suffix_start = tokenizer->cursor;
  while (bosonoga_at_word_char(tokenizer)) {
    bosonoga_consume_char(tokenizer);
  }
  g_autofree gchar *suffix =
      g_strndup(suffix_start, (gsize)(tokenizer->cursor - suffix_start));

  if (g_str_equal(suffix, "_i32")) {
    if (has_fraction) {
      return bosonoga_lex_error(tokenizer, token->line, token->column,
                                "an integer cannot have a fraction", error);
    }
    gint64 value = 0;
    if (!g_ascii_string_to_signed(number, 10, G_MININT32, G_MAXINT32, &value,
                                  NULL)) {
      return bosonoga_lex_error(tokenizer, token->line, token->column,
                                "integer does not fit in 32 bits", error);
    }
    token->kind = BOSONOGA_TOKEN_INTEGER;
    token->integer = (gint32)value;
    return TRUE;
  }
  if (g_str_equal(suffix, "_u32")) {
    if (has_fraction) {
      return bosonoga_lex_error(tokenizer, token->line, token->column,
                                "an integer cannot have a fraction", error);
    }
    guint64 value = 0;
    if (!g_ascii_string_to_unsigned(number, 10, 0, G_MAXUINT32, &value, NULL)) {
      return bosonoga_lex_error(tokenizer, token->line, token->column,
                                "_u32 number is outside the u32 range", error);
    }
    token->kind = BOSONOGA_TOKEN_UNSIGNED;
    token->unsigned_integer = (guint32)value;
    return TRUE;
  }
  if (g_str_equal(suffix, "_f32")) {
    const gdouble value = g_ascii_strtod(g_strdelimit(number, "d", '.'), NULL);
    if (ABS(value) > (gdouble)G_MAXFLOAT) {
      return bosonoga_lex_error(tokenizer, token->line, token->column,
                                "real number does not fit in 32 bits", error);
    }
    token->kind = BOSONOGA_TOKEN_REAL;
    token->real = (gfloat)value;
    return TRUE;
  }
  return bosonoga_lex_error(tokenizer, token->line, token->column,
                            "number must have the suffix _i32, _u32 or _f32",
                            error);
}

void bosonoga_tokenizer_init(BosonogaTokenizer *tokenizer, const gchar *path,
                             const gchar *source, gsize length) {
  const BosonogaTokenizer initial = {path, source, source + length, 1, 1, {0}};
  *tokenizer = initial;
}

gboolean bosonoga_tokenizer_advance(BosonogaTokenizer *tokenizer,
                                    GError **error) {
  bosonoga_skip_blanks_and_comments(tokenizer);

  BosonogaToken token = {BOSONOGA_TOKEN_END,
                         tokenizer->cursor,
                         0,
                         tokenizer->line,
                         tokenizer->column,
                         0,
                         0,
                         0.0f};
  if (tokenizer->cursor < tokenizer->end) {
    const gchar first = *tokenizer->cursor;
    if (first == '"') {
      token.kind = BOSONOGA_TOKEN_STRING;
      bosonoga_consume_char(tokenizer);
      while (tokenizer->cursor == tokenizer->end || *tokenizer->cursor != '"') {
        if (tokenizer->cursor == tokenizer->end || *tokenizer->cursor == '\n') {
          return bosonoga_lex_error(tokenizer, token.line, token.column,
                                    "unterminated string", error);
        }
        if (*tokenizer->cursor == '\\') {
          return bosonoga_lex_error(
              tokenizer, tokenizer->line, tokenizer->column,
              "escape sequences are not supported", error);
        }
        if (*tokenizer->cursor == '\0') {
          return bosonoga_lex_error(tokenizer, tokenizer->line,
                                    tokenizer->column, "NUL byte in string",
                                    error);
        }
        bosonoga_consume_char(tokenizer);
      }
      bosonoga_consume_char(tokenizer);
    } else if (bosonoga_at_number(tokenizer)) {
      if (!bosonoga_lex_number(tokenizer, &token, error)) {
        return FALSE;
      }
    } else if (g_ascii_isalpha(first) || first == '_') {
      token.kind = BOSONOGA_TOKEN_WORD;
      while (bosonoga_at_word_char(tokenizer)) {
        bosonoga_consume_char(tokenizer);
      }
    } else {
      return bosonoga_lex_error(tokenizer, token.line, token.column,
                                "unexpected character", error);
    }
    token.length = (gsize)(tokenizer->cursor - token.text);
  }

  tokenizer->token = token;
  return TRUE;
}
