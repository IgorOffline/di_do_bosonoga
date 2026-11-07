#ifndef DARIA_SETTINGS_H_
#define DARIA_SETTINGS_H_

#include <glib.h>

typedef struct {
  gchar *name;
  gchar *version;
  gint gfx_width;
  gint gfx_height;
} DariaSettings;

void daria_settings_clear(DariaSettings *settings);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(DariaSettings, daria_settings_clear)

// Reads regina.ini into settings, which must be zeroed and is cleared
// by the caller, also on failure.
gboolean daria_read_settings(DariaSettings *settings, GError **error);

#endif  // DARIA_SETTINGS_H_
