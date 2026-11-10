#include "bosonoga/settings.h"

#include <string.h>

#include "bosonoga/bosonoga.h"
#include "tokenizer.h"
#include "value.h"

#define BOSONOGA_SETTINGS_GROUP_META "meta"
#define BOSONOGA_SETTINGS_GROUP_GFX "gfx"
#define BOSONOGA_SETTINGS_GROUP_COLOR "color"
#define BOSONOGA_SETTINGS_GROUP_GLOBAL "global"
#define BOSONOGA_SETTINGS_KEY_BACKGROUND "background"
#define BOSONOGA_COLOR_HEX_LENGTH 6

typedef struct {
  GKeyFile *key_file;
  const gchar *path;
} BosonogaSettingsFile;

void bosonoga_settings_clear(BosonogaSettings *settings) {
  g_free(settings->name);
  g_free(settings->version);
  if (settings->colors != NULL) {
    g_hash_table_unref(settings->colors);
  }
  if (settings->globals != NULL) {
    g_hash_table_unref(settings->globals);
  }
}

gboolean bosonoga_settings_global(const BosonogaSettings *settings,
                                  const gchar *key, BosonogaValue *value) {
  const BosonogaValue *found = g_hash_table_lookup(settings->globals, key);
  if (found == NULL) {
    return FALSE;
  }
  *value = *found;
  return TRUE;
}

gboolean bosonoga_settings_color(const BosonogaSettings *settings,
                                 const gchar *key, guint32 *color) {
  gpointer value = NULL;
  if (!g_hash_table_lookup_extended(settings->colors, key, NULL, &value)) {
    return FALSE;
  }
  *color = GPOINTER_TO_UINT(value);
  return TRUE;
}

// GKeyFile keeps the quotes around a value.
static gchar *bosonoga_read_text_setting(const BosonogaSettingsFile *file,
                                         const gchar *group, const gchar *key,
                                         GError **error) {
  GError *read_error = NULL;
  g_autofree gchar *value =
      g_key_file_get_string(file->key_file, group, key, &read_error);
  if (value == NULL) {
    g_propagate_prefixed_error(error, read_error, "%s: ", file->path);
    return NULL;
  }
  const gsize length = strlen(value);
  if (length >= 2 && value[0] == '"' && value[length - 1] == '"') {
    return g_strndup(value + 1, length - 2);
  }
  return g_steal_pointer(&value);
}

static gboolean bosonoga_read_positive_setting(const BosonogaSettingsFile *file,
                                               const gchar *key, gint *value,
                                               GError **error) {
  GError *read_error = NULL;
  *value = g_key_file_get_integer(file->key_file, BOSONOGA_SETTINGS_GROUP_GFX,
                                  key, &read_error);
  if (read_error != NULL) {
    g_propagate_prefixed_error(error, read_error, "%s: ", file->path);
    return FALSE;
  }
  if (*value <= 0) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
                "%s: %s must be positive", file->path, key);
    return FALSE;
  }
  return TRUE;
}

static gboolean bosonoga_read_color_setting(const BosonogaSettingsFile *file,
                                            const gchar *key, guint32 *color,
                                            GError **error) {
  g_autofree gchar *text = bosonoga_read_text_setting(
      file, BOSONOGA_SETTINGS_GROUP_COLOR, key, error);
  if (text == NULL) {
    return FALSE;
  }
  gboolean valid = strlen(text) == BOSONOGA_COLOR_HEX_LENGTH;
  for (gsize i = 0; valid && i < BOSONOGA_COLOR_HEX_LENGTH; ++i) {
    valid = g_ascii_isxdigit(text[i]) && !g_ascii_islower(text[i]);
  }
  if (!valid) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
                "%s: %s must be %d uppercase hex digits", file->path, key,
                BOSONOGA_COLOR_HEX_LENGTH);
    return FALSE;
  }
  *color = (guint32)g_ascii_strtoull(text, NULL, 16);
  return TRUE;
}

static gboolean bosonoga_read_color_settings(const BosonogaSettingsFile *file,
                                             BosonogaSettings *settings,
                                             GError **error) {
  settings->colors =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  if (!g_key_file_has_group(file->key_file, BOSONOGA_SETTINGS_GROUP_COLOR)) {
    return TRUE;
  }
  GError *read_error = NULL;
  g_auto(GStrv) keys = g_key_file_get_keys(
      file->key_file, BOSONOGA_SETTINGS_GROUP_COLOR, NULL, &read_error);
  if (keys == NULL) {
    g_propagate_prefixed_error(error, read_error, "%s: ", file->path);
    return FALSE;
  }
  for (gsize i = 0; keys[i] != NULL; ++i) {
    guint32 color = 0;
    if (!bosonoga_read_color_setting(file, keys[i], &color, error)) {
      return FALSE;
    }
    g_hash_table_insert(settings->colors, g_strdup(keys[i]),
                        GUINT_TO_POINTER(color));
  }
  if (g_key_file_has_group(file->key_file, BOSONOGA_SETTINGS_GROUP_GFX) &&
      !bosonoga_settings_color(settings, BOSONOGA_SETTINGS_KEY_BACKGROUND,
                               &settings->background_color)) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
                "%s: the group %s needs the key %s", file->path,
                BOSONOGA_SETTINGS_GROUP_COLOR,
                BOSONOGA_SETTINGS_KEY_BACKGROUND);
    return FALSE;
  }
  return TRUE;
}

// One number, like 100d0_f32, read by the tokenizer of programs.
static gboolean bosonoga_read_global_setting(const BosonogaSettingsFile *file,
                                             const gchar *key,
                                             BosonogaValue *value,
                                             GError **error) {
  g_autofree gchar *text = bosonoga_read_text_setting(
      file, BOSONOGA_SETTINGS_GROUP_GLOBAL, key, error);
  if (text == NULL) {
    return FALSE;
  }
  BosonogaTokenizer tokenizer;
  bosonoga_tokenizer_init(&tokenizer, file->path, text, strlen(text));
  if (bosonoga_tokenizer_advance(&tokenizer, NULL)) {
    const BosonogaToken number = tokenizer.token;
    if (number.kind == BOSONOGA_TOKEN_LITERAL &&
        bosonoga_kind_is_number(number.value.kind) &&
        bosonoga_tokenizer_advance(&tokenizer, NULL) &&
        tokenizer.token.kind == BOSONOGA_TOKEN_END) {
      *value = number.value;
      return TRUE;
    }
  }
  g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
              "%s: %s must be one number, like 100_i32 or 100d0_f32",
              file->path, key);
  return FALSE;
}

static gboolean bosonoga_read_global_settings(const BosonogaSettingsFile *file,
                                              BosonogaSettings *settings,
                                              GError **error) {
  settings->globals =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  if (!g_key_file_has_group(file->key_file, BOSONOGA_SETTINGS_GROUP_GLOBAL)) {
    return TRUE;
  }
  g_auto(GStrv) keys = g_key_file_get_keys(
      file->key_file, BOSONOGA_SETTINGS_GROUP_GLOBAL, NULL, NULL);
  for (gsize i = 0; keys != NULL && keys[i] != NULL; ++i) {
    BosonogaValue *value = g_new0(BosonogaValue, 1);
    if (!bosonoga_read_global_setting(file, keys[i], value, error)) {
      g_free(value);
      return FALSE;
    }
    g_hash_table_insert(settings->globals, g_strdup(keys[i]), value);
  }
  return TRUE;
}

gboolean bosonoga_read_settings(const gchar *path, BosonogaSettings *settings,
                                GError **error) {
  g_autoptr(GKeyFile) key_file = g_key_file_new();
  g_autofree gchar *display_path = g_filename_display_name(path);
  const BosonogaSettingsFile file = {key_file, display_path};
  GError *load_error = NULL;
  if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE,
                                 &load_error)) {
    g_propagate_prefixed_error(error, load_error, "%s: ", display_path);
    return FALSE;
  }
  settings->name = bosonoga_read_text_setting(
      &file, BOSONOGA_SETTINGS_GROUP_META, "name", error);
  if (settings->name == NULL) {
    return FALSE;
  }
  settings->version = bosonoga_read_text_setting(
      &file, BOSONOGA_SETTINGS_GROUP_META, "version", error);
  if (settings->version == NULL) {
    return FALSE;
  }
  if (g_key_file_has_group(key_file, BOSONOGA_SETTINGS_GROUP_GFX) &&
      !(bosonoga_read_positive_setting(&file, "width", &settings->gfx_width,
                                       error) &&
        bosonoga_read_positive_setting(&file, "height", &settings->gfx_height,
                                       error))) {
    return FALSE;
  }
  return bosonoga_read_color_settings(&file, settings, error) &&
         bosonoga_read_global_settings(&file, settings, error);
}
