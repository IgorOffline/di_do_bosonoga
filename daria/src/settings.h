#ifndef DARIA_SETTINGS_H_
#define DARIA_SETTINGS_H_

#include <glib.h>

#include "daria.h"

typedef struct {
  gchar *name;
  gchar *version;
  gint gfx_width;
  gint gfx_height;
  GHashTable *colors;        // Every key of the [color] group, to its 0xRRGGBB.
  guint32 background_color;  // The value of the required key background.
  GHashTable *globals;  // Every key of the [global] group, to a DariaValue.
} DariaSettings;

void daria_settings_clear(DariaSettings *settings);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(DariaSettings, daria_settings_clear)

// Returns whether the [color] group has the key, and stores its value in color.
gboolean daria_settings_color(const DariaSettings *settings, const gchar *key,
                              guint32 *color);

// Returns whether the [global] group, which is optional, has the key, and
// stores its value in value.
gboolean daria_settings_global(const DariaSettings *settings, const gchar *key,
                               DariaValue *value);

// Reads regina.ini into settings, which must be zeroed and is cleared
// by the caller, also on failure.
gboolean daria_read_settings(DariaSettings *settings, GError **error);

#endif  // DARIA_SETTINGS_H_
