#include "settings.h"

#include <string.h>

#include "daria.h"

#define DARIA_SETTINGS_PATH "regina.ini"
#define DARIA_SETTINGS_GROUP_META "meta"
#define DARIA_SETTINGS_GROUP_GFX "gfx"
#define DARIA_SETTINGS_GROUP_COLOR "color"
#define DARIA_COLOR_HEX_LENGTH 6

void daria_settings_clear(DariaSettings *settings) {
  g_free(settings->name);
  g_free(settings->version);
}

// Returns a new string with the value of the key. GKeyFile does not remove the
// quotes around a value, so that is done here.
static gchar *daria_read_text_setting(GKeyFile *key_file, const gchar *group,
                                      const gchar *key, GError **error) {
  GError *read_error = NULL;
  g_autofree gchar *value =
      g_key_file_get_string(key_file, group, key, &read_error);
  if (value == NULL) {
    g_propagate_prefixed_error(error, read_error, "%s: ", DARIA_SETTINGS_PATH);
    return NULL;
  }
  const gsize length = strlen(value);
  if (length >= 2 && value[0] == '"' && value[length - 1] == '"') {
    return g_strndup(value + 1, length - 2);
  }
  return g_steal_pointer(&value);
}

static gboolean daria_read_positive_setting(GKeyFile *key_file,
                                            const gchar *key, gint *value,
                                            GError **error) {
  GError *read_error = NULL;
  *value = g_key_file_get_integer(key_file, DARIA_SETTINGS_GROUP_GFX, key,
                                  &read_error);
  if (read_error != NULL) {
    g_propagate_prefixed_error(error, read_error, "%s: ", DARIA_SETTINGS_PATH);
    return FALSE;
  }
  if (*value <= 0) {
    g_set_error(error, DARIA_ERROR, DARIA_ERROR_RUNTIME,
                "%s: %s must be positive", DARIA_SETTINGS_PATH, key);
    return FALSE;
  }
  return TRUE;
}

// Reads a color written as six hex digits, RRGGBB.
static gboolean daria_read_color_setting(GKeyFile *key_file, const gchar *key,
                                         gfloat color[DARIA_COLOR_CHANNELS],
                                         GError **error) {
  g_autofree gchar *text =
      daria_read_text_setting(key_file, DARIA_SETTINGS_GROUP_COLOR, key, error);
  if (text == NULL) {
    return FALSE;
  }
  gboolean valid = strlen(text) == DARIA_COLOR_HEX_LENGTH;
  for (gsize i = 0; valid && i < DARIA_COLOR_HEX_LENGTH; ++i) {
    valid = g_ascii_isxdigit(text[i]);
  }
  if (!valid) {
    g_set_error(error, DARIA_ERROR, DARIA_ERROR_RUNTIME,
                "%s: %s must be %d hex digits", DARIA_SETTINGS_PATH, key,
                DARIA_COLOR_HEX_LENGTH);
    return FALSE;
  }
  for (gsize channel = 0; channel < 3; ++channel) {
    const gint high = g_ascii_xdigit_value(text[channel * 2]);
    const gint low = g_ascii_xdigit_value(text[channel * 2 + 1]);
    color[channel] = (gfloat)(high * 16 + low) / 255.0f;
  }
  color[3] = 1.0f;
  return TRUE;
}

gboolean daria_read_settings(DariaSettings *settings, GError **error) {
  g_autoptr(GKeyFile) key_file = g_key_file_new();
  GError *load_error = NULL;
  if (!g_key_file_load_from_file(key_file, DARIA_SETTINGS_PATH, G_KEY_FILE_NONE,
                                 &load_error)) {
    g_propagate_prefixed_error(error, load_error, "%s: ", DARIA_SETTINGS_PATH);
    return FALSE;
  }
  settings->name = daria_read_text_setting(key_file, DARIA_SETTINGS_GROUP_META,
                                           "name", error);
  if (settings->name == NULL) {
    return FALSE;
  }
  settings->version = daria_read_text_setting(
      key_file, DARIA_SETTINGS_GROUP_META, "version", error);
  if (settings->version == NULL) {
    return FALSE;
  }
  return daria_read_positive_setting(key_file, "width", &settings->gfx_width,
                                     error) &&
         daria_read_positive_setting(key_file, "height", &settings->gfx_height,
                                     error) &&
         daria_read_color_setting(key_file, "background",
                                  settings->colors[DARIA_COLOR_BACKGROUND],
                                  error) &&
         daria_read_color_setting(key_file, "rectangle",
                                  settings->colors[DARIA_COLOR_RECTANGLE],
                                  error);
}
