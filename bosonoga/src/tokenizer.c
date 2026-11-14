#include "tokenizer.h"

#include <string.h>

#include "bosonoga/bosonoga.h"
#include "value.h"
#include "xml.h"

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

// Each integer type fits in the range of gint64 and guint64 together.
static gboolean bosonoga_parse_whole(const gchar *digits,
                                     BosonogaValueKind kind,
                                     BosonogaValue *value) {
  gint64 negative = 0;
  guint64 positive = 0;
  if (digits[0] == '-') {
    return g_ascii_string_to_signed(digits, 10, G_MININT64, 0, &negative,
                                    NULL) &&
           bosonoga_set_whole(value, kind, negative);
  }
  return g_ascii_string_to_unsigned(digits, 10, 0, G_MAXUINT64, &positive,
                                    NULL) &&
         bosonoga_set_whole(value, kind, positive);
}

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

  BosonogaValueKind kind = BOSONOGA_VALUE_I32;
  if (suffix[0] != '_' ||
      !bosonoga_value_kind_named(suffix + 1, strlen(suffix + 1), &kind)) {
    return bosonoga_lex_error(tokenizer, token->line, token->column,
                              "number must have the suffix _i32, _i64, _u32, "
                              "_u64, _f32 or _f64",
                              error);
  }
  if (!bosonoga_kind_is_number(kind)) {
    if (has_fraction || token->text[0] == '-') {
      return bosonoga_lex_error(tokenizer, token->line, token->column,
                                "an index is digits and a type, like 1_str",
                                error);
    }
    token->kind = BOSONOGA_TOKEN_INDEXED_TYPE;
    return TRUE;
  }
  const gboolean is_integer = bosonoga_kind_is_integer(kind);
  if (is_integer && has_fraction) {
    return bosonoga_lex_error(tokenizer, token->line, token->column,
                              "an integer cannot have a fraction", error);
  }
  if (is_integer ? !bosonoga_parse_whole(number, kind, &token->value)
                 : !bosonoga_set_real(
                       &token->value, kind,
                       g_ascii_strtod(g_strdelimit(number, "d", '.'), NULL))) {
    g_autofree gchar *message = g_strdup_printf(
        "the number is outside the %s range", bosonoga_value_kind_name(kind));
    return bosonoga_lex_error(tokenizer, token->line, token->column, message,
                              error);
  }
  token->kind = BOSONOGA_TOKEN_LITERAL;
  return TRUE;
}

static gboolean bosonoga_lex_string(BosonogaTokenizer *tokenizer,
                                    BosonogaToken *token, GError **error) {
  bosonoga_consume_char(tokenizer);
  token->value.kind = BOSONOGA_VALUE_STR;
  token->value.str.text = tokenizer->cursor;
  while (tokenizer->cursor == tokenizer->end || *tokenizer->cursor != '"') {
    if (tokenizer->cursor == tokenizer->end || *tokenizer->cursor == '\n') {
      return bosonoga_lex_error(tokenizer, token->line, token->column,
                                "unterminated string", error);
    }
    if (*tokenizer->cursor == '\\') {
      return bosonoga_lex_error(tokenizer, tokenizer->line, tokenizer->column,
                                "escape sequences are not supported", error);
    }
    if (*tokenizer->cursor == '\0') {
      return bosonoga_lex_error(tokenizer, tokenizer->line, tokenizer->column,
                                "NUL byte in string", error);
    }
    bosonoga_consume_char(tokenizer);
  }
  token->value.str.length = (gsize)(tokenizer->cursor - token->value.str.text);
  bosonoga_consume_char(tokenizer);

  const gchar *suffix = tokenizer->cursor;
  while (bosonoga_at_word_char(tokenizer)) {
    bosonoga_consume_char(tokenizer);
  }
  const gsize suffix_length = (gsize)(tokenizer->cursor - suffix);
  if (suffix_length == strlen(BOSONOGA_XML_STRING_SUFFIX) &&
      memcmp(suffix, BOSONOGA_XML_STRING_SUFFIX, suffix_length) == 0) {
    GError *xml_error = NULL;
    if (!bosonoga_xml_check_fragment(token->value.str.text,
                                     token->value.str.length, &xml_error)) {
      g_autofree gchar *message = g_strdup_printf(
          "an xml_str is well-formed XML, but %s", xml_error->message);
      g_error_free(xml_error);
      return bosonoga_lex_error(tokenizer, token->line, token->column, message,
                                error);
    }
    token->value.kind = BOSONOGA_VALUE_XML_STR;
  } else if (suffix_length != strlen(BOSONOGA_STRING_SUFFIX) ||
             memcmp(suffix, BOSONOGA_STRING_SUFFIX, suffix_length) != 0) {
    return bosonoga_lex_error(
        tokenizer, token->line, token->column,
        "a string must have the suffix " BOSONOGA_STRING_SUFFIX
        " or " BOSONOGA_XML_STRING_SUFFIX
        ", as in \"text\"" BOSONOGA_STRING_SUFFIX,
        error);
  }
  token->kind = BOSONOGA_TOKEN_LITERAL;
  return TRUE;
}

static gboolean bosonoga_text_is(const gchar *text, gsize length,
                                 const gchar *word) {
  return length == strlen(word) && memcmp(text, word, length) == 0;
}

static void bosonoga_lex_bool(BosonogaToken *token, gsize length) {
  const gboolean is_true =
      bosonoga_text_is(token->text, length, BOSONOGA_TRUE_WORD);
  if (is_true || bosonoga_text_is(token->text, length, BOSONOGA_FALSE_WORD)) {
    token->kind = BOSONOGA_TOKEN_LITERAL;
    token->value.kind = BOSONOGA_VALUE_BOOL;
    token->value.boolean = is_true;
  }
}

void bosonoga_tokenizer_init(BosonogaTokenizer *tokenizer, const gchar *path,
                             const gchar *source, gsize length) {
  const BosonogaTokenizer initial = {path, source, source + length, 1, 1, {0}};
  *tokenizer = initial;
}

gboolean bosonoga_tokenizer_advance(BosonogaTokenizer *tokenizer,
                                    GError **error) {
  bosonoga_skip_blanks_and_comments(tokenizer);

  BosonogaToken token = {
      .kind = BOSONOGA_TOKEN_END,
      .path = tokenizer->path,
      .text = tokenizer->cursor,
      .line = tokenizer->line,
      .column = tokenizer->column,
  };
  if (tokenizer->cursor < tokenizer->end) {
    const gchar first = *tokenizer->cursor;
    if (first == '"') {
      if (!bosonoga_lex_string(tokenizer, &token, error)) {
        return FALSE;
      }
    } else if (bosonoga_at_number(tokenizer)) {
      if (!bosonoga_lex_number(tokenizer, &token, error)) {
        return FALSE;
      }
    } else if (g_ascii_isalpha(first) || first == '_') {
      token.kind = BOSONOGA_TOKEN_WORD;
      while (bosonoga_at_word_char(tokenizer)) {
        bosonoga_consume_char(tokenizer);
      }
      bosonoga_lex_bool(&token, (gsize)(tokenizer->cursor - token.text));
    } else {
      return bosonoga_lex_error(tokenizer, token.line, token.column,
                                "unexpected character", error);
    }
    token.length = (gsize)(tokenizer->cursor - token.text);
  }

  tokenizer->token = token;
  return TRUE;
}
