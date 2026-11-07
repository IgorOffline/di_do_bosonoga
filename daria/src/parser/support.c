#include <stdarg.h>
#include <string.h>

#include "parser/internal.h"

#define DARIA_MESSAGE_TOKEN_MAX 32

static const gchar *const daria_reserved_words[] = {
    "main",         "di",
    "do",           "info",
    "var",          "set",
    "is",           "if",
    "else",         "and",
    "or",           "gt",
    "lt",           "eq",
    "not_eq",       "eq_3",
    "eq_6",         "not_eq_3",
    "not_eq_6",     "nihil",
    "return",       "exit_success",
    "exit_failure", "with",
    "to_add",       "to_subtract",
    "to_multiply",  "to_divide",
    "to_f32",       "to_i32",
    "to_sin",       "to_cos",
    "to_atan2",     "to_sqrt",
    "to_pow",       "shift_or",
    "shift_has",    "sleep_ms",
    "gfx_init",     "gfx_frame",
    "gfx_main",     "gfx_cleanup",
    "gfx_clear",    "draw_rectangle",
    "gfx_global",   DARIA_DELTA_TIME_WORD,
};

gboolean daria_token_is_word(const DariaToken *token, const gchar *word) {
  return token->kind == DARIA_TOKEN_WORD && token->length == strlen(word) &&
         memcmp(token->text, word, token->length) == 0;
}

gboolean daria_token_is_numbered(const DariaToken *token, const gchar *prefix) {
  const gsize prefix_length = strlen(prefix);
  if (token->kind != DARIA_TOKEN_WORD || token->length <= prefix_length ||
      memcmp(token->text, prefix, prefix_length) != 0) {
    return FALSE;
  }
  for (gsize i = prefix_length; i < token->length; ++i) {
    if (!g_ascii_isdigit(token->text[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

// Returns a new string with what follows the prefix of the word, or NULL if the
// token is not a word that starts with the prefix.
static gchar *daria_token_key(const DariaToken *token, const gchar *prefix) {
  const gsize prefix_length = strlen(prefix);
  if (token->kind != DARIA_TOKEN_WORD || token->length <= prefix_length ||
      memcmp(token->text, prefix, prefix_length) != 0) {
    return NULL;
  }
  return g_strndup(token->text + prefix_length, token->length - prefix_length);
}

gboolean daria_token_setting(const DariaParser *parser, const DariaToken *token,
                             DariaValue *value) {
  g_autofree gchar *color_key = daria_token_key(token, DARIA_COLOR_PREFIX);
  if (color_key != NULL) {
    value->kind = DARIA_VALUE_COLOR;
    return daria_settings_color(parser->settings, color_key, &value->color);
  }
  g_autofree gchar *global_key = daria_token_key(token, DARIA_GLOBAL_PREFIX);
  return global_key != NULL &&
         daria_settings_global(parser->settings, global_key, value);
}

gboolean daria_token_is_reserved(const DariaParser *parser,
                                 const DariaToken *token) {
  for (gsize i = 0; i < G_N_ELEMENTS(daria_reserved_words); ++i) {
    if (daria_token_is_word(token, daria_reserved_words[i])) {
      return TRUE;
    }
  }
  DariaValue value;
  return daria_token_is_numbered(token, DARIA_SHIFT_PREFIX) ||
         daria_token_is_numbered(token, DARIA_LOOP_PREFIX) ||
         daria_token_setting(parser, token, &value);
}

const gchar *daria_value_kind_name(DariaValueKind kind) {
  switch (kind) {
    case DARIA_VALUE_I32:
      return "i32";
    case DARIA_VALUE_F32:
      return "f32";
    case DARIA_VALUE_FLAGS:
      return "flags";
    case DARIA_VALUE_COLOR:
      return "color";
  }
  g_return_val_if_reached("unknown");
}

gchar *daria_token_shown(const DariaToken *token) {
  return g_utf8_make_valid(token->text,
                           (gssize)MIN(token->length, DARIA_MESSAGE_TOKEN_MAX));
}

gboolean daria_fail_at(const DariaParser *parser, const DariaToken *token,
                       GError **error, DariaError code, const gchar *format,
                       ...) {
  va_list args;
  va_start(args, format);
  g_autofree gchar *message = g_strdup_vprintf(format, args);
  va_end(args);
  g_set_error(error, DARIA_ERROR, (gint)code,
              "%s:%" G_GSIZE_FORMAT ":%" G_GSIZE_FORMAT ": %s",
              parser->tokenizer.path, token->line, token->column, message);
  return FALSE;
}

gboolean daria_parse_error(const DariaParser *parser, const gchar *expected,
                           GError **error) {
  const DariaToken *token = &parser->tokenizer.token;
  if (token->kind == DARIA_TOKEN_END) {
    return daria_fail_at(parser, token, error, DARIA_ERROR_SYNTAX,
                         "expected %s, got end of file", expected);
  }
  g_autofree gchar *shown = daria_token_shown(token);
  return daria_fail_at(parser, token, error, DARIA_ERROR_SYNTAX,
                       "expected %s, got '%s'", expected, shown);
}

gboolean daria_advance(DariaParser *parser, GError **error) {
  return daria_tokenizer_advance(&parser->tokenizer, error);
}

gboolean daria_at_word(const DariaParser *parser, const gchar *word) {
  return daria_token_is_word(&parser->tokenizer.token, word);
}

gboolean daria_expect_word(DariaParser *parser, const gchar *word,
                           GError **error) {
  if (!daria_at_word(parser, word)) {
    return daria_parse_error(parser, word, error);
  }
  return daria_advance(parser, error);
}

DariaOp *daria_emit(DariaParser *parser, DariaOpKind kind,
                    const DariaToken *token) {
  g_array_set_size(parser->ops, parser->ops->len + 1);
  DariaOp *op = &g_array_index(parser->ops, DariaOp, parser->ops->len - 1);
  op->kind = kind;
  op->line = token->line;
  op->column = token->column;
  return op;
}

void daria_patch_jumps_to_here(DariaParser *parser, const GArray *jumps) {
  for (guint i = 0; i < jumps->len; ++i) {
    const guint jump = g_array_index(jumps, guint, i);
    g_array_index(parser->ops, DariaOp, jump).target = parser->ops->len;
  }
}

gboolean daria_numbered_value(const DariaToken *token, const gchar *prefix,
                              guint64 max, guint64 *value) {
  const gsize prefix_length = strlen(prefix);
  g_autofree gchar *digits =
      g_strndup(token->text + prefix_length, token->length - prefix_length);
  return g_ascii_string_to_unsigned(digits, 10, 0, max, value, NULL);
}
