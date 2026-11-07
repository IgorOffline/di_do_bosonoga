#include "settings.h"

#include <string.h>

#include "daria.h"

#define DARIA_SETTINGS_PATH "regina.ini"
#define DARIA_SETTINGS_GROUP_META "meta"
#define DARIA_SETTINGS_GROUP_GFX "gfx"

void daria_settings_clear(DariaSettings *settings) {
  g_free(settings->name);
  g_free(settings->version);
}

// Returns a new string with the value of the key. GKeyFile does not remove the
// quotes around a value, so that is done here.
static gchar *daria_read_text_setting(GKeyFile *key_file, const gchar *key,
                                      GError **error) {
  GError *read_error = NULL;
  g_autofree gchar *value = g_key_file_get_string(
      key_file, DARIA_SETTINGS_GROUP_META, key, &read_error);
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

gboolean daria_read_settings(DariaSettings *settings, GError **error) {
  g_autoptr(GKeyFile) key_file = g_key_file_new();
  GError *load_error = NULL;
  if (!g_key_file_load_from_file(key_file, DARIA_SETTINGS_PATH, G_KEY_FILE_NONE,
                                 &load_error)) {
    g_propagate_prefixed_error(error, load_error, "%s: ", DARIA_SETTINGS_PATH);
    return FALSE;
  }
  settings->name = daria_read_text_setting(key_file, "name", error);
  if (settings->name == NULL) {
    return FALSE;
  }
  settings->version = daria_read_text_setting(key_file, "version", error);
  if (settings->version == NULL) {
    return FALSE;
  }
  return daria_read_positive_setting(key_file, "width", &settings->gfx_width,
                                     error) &&
         daria_read_positive_setting(key_file, "height", &settings->gfx_height,
                                     error);
}
