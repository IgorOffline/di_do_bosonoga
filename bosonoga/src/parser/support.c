#include <stdarg.h>
#include <string.h>

#include "parser/internal.h"

#define BOSONOGA_MESSAGE_TOKEN_MAX 32

static const gchar *const bosonoga_reserved_words[] = {
    "bosonoga_main",
    "di",
    "do",
    "info",
    "var",
    "val",
    "set",
    "unset",
    "new",
    "is",
    "if",
    "else",
    "and",
    "or",
    "gt",
    "lt",
    "eq",
    "not_eq",
    "eq_3",
    "eq_6",
    "not_eq_3",
    "not_eq_6",
    "nihil",
    "return",
    "exit_success",
    "exit_failure",
    "with",
    "to_add",
    "to_subtract",
    "to_multiply",
    "to_divide",
    "const",
    "io_raw_import",
    "to_f64",
    "math_to_log",
    "math_to_log10",
    "math_to_log1p",
    "math_to_expm1",
    "math_to_hypot",
    "math_to_exp",
    "math_to_asin",
    "math_to_acos",
    "math_to_tan",
    "to_f32",
    "to_i32",
    "to_i64",
    "to_u32",
    "to_u64",
    "math_to_sin",
    "math_to_cos",
    "math_to_atan2",
    "math_to_sqrt",
    "math_to_pow",
    "math_to_abs",
    "math_to_min",
    "math_to_max",
    "math_to_floor",
    "math_to_ceil",
    "math_to_round",
    "shift_or",
    "shift_has",
    "sleep_ms",
    "gfx_await_next_frame",
    "gfx_init",
    "gfx_frame",
    "gfx_main",
    "gfx_cleanup",
    "gfx_clear",
    "gfx_draw_rectangle",
    "gfx_draw_hex",
    "gfx_draw_circle",
    "gfx_draw_text",
    "gfx_draw_icon",
    "gfx_key_down",
    BOSONOGA_KEY_WORD,
    "bosonoga_global",
    BOSONOGA_DELTA_TIME_WORD,
    "while",
    "break",
    "continue",
    "to_mod",
    "ge",
    "le",
    "not",
    "def",
    "ret",
    "call",
    "to",
    "array",
    "row",
    "rows",
    "len_col",
    "len_row",
    "expect",
    "at",
    "of",
    "as",
    BOSONOGA_VOID_WORD,
};

gboolean bosonoga_token_is_word(const BosonogaToken *token, const gchar *word) {
  return token->kind == BOSONOGA_TOKEN_WORD && token->length == strlen(word) &&
         memcmp(token->text, word, token->length) == 0;
}

gboolean bosonoga_token_is_numbered(const BosonogaToken *token,
                                    const gchar *prefix) {
  const gsize prefix_length = strlen(prefix);
  if (token->kind != BOSONOGA_TOKEN_WORD || token->length <= prefix_length ||
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

// NULL unless the token is a word that starts with the prefix.
static gchar *bosonoga_token_key(const BosonogaToken *token,
                                 const gchar *prefix) {
  const gsize prefix_length = strlen(prefix);
  if (token->kind != BOSONOGA_TOKEN_WORD || token->length <= prefix_length ||
      memcmp(token->text, prefix, prefix_length) != 0) {
    return NULL;
  }
  return g_strndup(token->text + prefix_length, token->length - prefix_length);
}

gboolean bosonoga_token_setting(const BosonogaParser *parser,
                                const BosonogaToken *token,
                                BosonogaValue *value) {
  g_autofree gchar *color_key =
      bosonoga_token_key(token, BOSONOGA_COLOR_PREFIX);
  if (color_key != NULL) {
    value->kind = BOSONOGA_VALUE_COLOR;
    return bosonoga_settings_color(parser->settings, color_key, &value->color);
  }
  g_autofree gchar *key_key = bosonoga_token_key(token, BOSONOGA_KEY_PREFIX);
  if (key_key != NULL) {
    value->kind = BOSONOGA_VALUE_INPUT_KEY;
    return bosonoga_settings_key(parser->settings, key_key, &value->input_key);
  }
  // Only its kind, as the program numbers its fonts and icons when a value
  // names one.
  g_autofree gchar *font_key = bosonoga_token_key(token, BOSONOGA_FONT_PREFIX);
  if (font_key != NULL) {
    value->kind = BOSONOGA_VALUE_FONT;
    return bosonoga_settings_font(parser->settings, font_key) != NULL;
  }
  g_autofree gchar *icon_key = bosonoga_token_key(token, BOSONOGA_ICON_PREFIX);
  if (icon_key != NULL) {
    value->kind = BOSONOGA_VALUE_ICON;
    return bosonoga_settings_icon(parser->settings, icon_key) != NULL;
  }
  g_autofree gchar *global_key =
      bosonoga_token_key(token, BOSONOGA_GLOBAL_PREFIX);
  return global_key != NULL &&
         bosonoga_settings_global(parser->settings, global_key, value);
}

void bosonoga_name_asset(BosonogaParser *parser, const BosonogaToken *token,
                         BosonogaValue *value) {
  const gboolean is_font = value->kind == BOSONOGA_VALUE_FONT;
  g_autofree gchar *key = bosonoga_token_key(
      token, is_font ? BOSONOGA_FONT_PREFIX : BOSONOGA_ICON_PREFIX);
  GArray *assets = is_font ? parser->program->fonts : parser->program->icons;
  guint index = 0;
  while (index < assets->len &&
         g_strcmp0(g_array_index(assets, BosonogaProgramAsset, index).key,
                   key) != 0) {
    ++index;
  }
  if (index == assets->len) {
    const BosonogaSettingsAsset *setting =
        is_font ? bosonoga_settings_font(parser->settings, key)
                : bosonoga_settings_icon(parser->settings, key);
    const BosonogaProgramAsset asset = {
        .key = g_steal_pointer(&key),
        .text = g_strdup(setting->text),
        .path = g_strdup(setting->path),
    };
    g_array_append_val(assets, asset);
  }
  value->asset.index = index;
  value->asset.text = g_array_index(assets, BosonogaProgramAsset, index).text;
}

// Like ret_0_i32, and also ret_0 and ret_0_x, which are then wrong values.
gboolean bosonoga_token_is_result(const BosonogaToken *token) {
  const gsize prefix_length = strlen(BOSONOGA_RESULT_PREFIX);
  return token->kind == BOSONOGA_TOKEN_WORD && token->length > prefix_length &&
         memcmp(token->text, BOSONOGA_RESULT_PREFIX, prefix_length) == 0 &&
         g_ascii_isdigit(token->text[prefix_length]);
}

gboolean bosonoga_token_is_function_name(const BosonogaToken *token) {
  const gsize suffix_length = strlen(BOSONOGA_FUNCTION_SUFFIX);
  return token->kind == BOSONOGA_TOKEN_WORD && token->length > suffix_length &&
         memcmp(token->text + token->length - suffix_length,
                BOSONOGA_FUNCTION_SUFFIX, suffix_length) == 0;
}

gboolean bosonoga_token_is_reserved(const BosonogaParser *parser,
                                    const BosonogaToken *token) {
  for (gsize i = 0; i < G_N_ELEMENTS(bosonoga_reserved_words); ++i) {
    if (bosonoga_token_is_word(token, bosonoga_reserved_words[i])) {
      return TRUE;
    }
  }
  BosonogaValue value;
  return bosonoga_token_is_numbered(token, BOSONOGA_SHIFT_PREFIX) ||
         bosonoga_token_is_numbered(token, BOSONOGA_LOOP_PREFIX) ||
         bosonoga_token_is_result(token) ||
         bosonoga_token_setting(parser, token, &value);
}

gchar *bosonoga_token_shown(const BosonogaToken *token) {
  return g_utf8_make_valid(
      token->text, (gssize)MIN(token->length, BOSONOGA_MESSAGE_TOKEN_MAX));
}

gboolean bosonoga_fail_at(const BosonogaParser *parser,
                          const BosonogaToken *token, GError **error,
                          BosonogaError code, const gchar *format, ...) {
  va_list args;
  va_start(args, format);
  g_autofree gchar *message = g_strdup_vprintf(format, args);
  va_end(args);
  g_set_error(error, BOSONOGA_ERROR, (gint)code,
              "%s:%" G_GSIZE_FORMAT ":%" G_GSIZE_FORMAT ": %s",
              token->path != NULL ? token->path : parser->tokenizer.path,
              token->line, token->column, message);
  return FALSE;
}

gboolean bosonoga_parse_error(const BosonogaParser *parser,
                              const gchar *expected, GError **error) {
  const BosonogaToken *token = &parser->tokenizer.token;
  if (token->kind == BOSONOGA_TOKEN_END) {
    return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_SYNTAX,
                            "expected %s, got end of file", expected);
  }
  g_autofree gchar *shown = bosonoga_token_shown(token);
  return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_SYNTAX,
                          "expected %s, got '%s'", expected, shown);
}

gboolean bosonoga_advance(BosonogaParser *parser, GError **error) {
  if (!bosonoga_tokenizer_advance(&parser->tokenizer, error)) {
    return FALSE;
  }
  while (parser->tokenizer.token.kind == BOSONOGA_TOKEN_END &&
         parser->outer_tokenizers->len > 0) {
    const guint last = parser->outer_tokenizers->len - 1;
    parser->tokenizer =
        g_array_index(parser->outer_tokenizers, BosonogaTokenizer, last);
    g_array_set_size(parser->outer_tokenizers, last);
    if (!bosonoga_tokenizer_advance(&parser->tokenizer, error)) {
      return FALSE;
    }
  }
  return TRUE;
}

gboolean bosonoga_at_word(const BosonogaParser *parser, const gchar *word) {
  return bosonoga_token_is_word(&parser->tokenizer.token, word);
}

gboolean bosonoga_expect_word(BosonogaParser *parser, const gchar *word,
                              GError **error) {
  if (!bosonoga_at_word(parser, word)) {
    return bosonoga_parse_error(parser, word, error);
  }
  return bosonoga_advance(parser, error);
}

BosonogaOp *bosonoga_emit(BosonogaParser *parser, BosonogaOpKind kind,
                          const BosonogaToken *token) {
  g_array_set_size(parser->ops, parser->ops->len + 1);
  BosonogaOp *op =
      &g_array_index(parser->ops, BosonogaOp, parser->ops->len - 1);
  op->kind = kind;
  op->path = token->path;
  op->line = token->line;
  op->column = token->column;
  return op;
}

void bosonoga_patch_jumps(BosonogaParser *parser, const GArray *jumps,
                          guint target) {
  for (guint i = 0; i < jumps->len; ++i) {
    const guint jump = g_array_index(jumps, guint, i);
    g_array_index(parser->ops, BosonogaOp, jump).target = target;
  }
}

void bosonoga_patch_jumps_to_here(BosonogaParser *parser, const GArray *jumps) {
  bosonoga_patch_jumps(parser, jumps, parser->ops->len);
}

gboolean bosonoga_numbered_value(const BosonogaToken *token,
                                 const gchar *prefix, guint64 max,
                                 guint64 *value) {
  const gsize prefix_length = strlen(prefix);
  g_autofree gchar *digits =
      g_strndup(token->text + prefix_length, token->length - prefix_length);
  return g_ascii_string_to_unsigned(digits, 10, 0, max, value, NULL);
}
