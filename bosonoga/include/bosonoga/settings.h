#ifndef BOSONOGA_SETTINGS_H_
#define BOSONOGA_SETTINGS_H_

#include <bosonoga/bosonoga.h>
#include <glib.h>

// The name of the settings file of a project, by convention.
#define BOSONOGA_SETTINGS_FILE "regina.ini"

typedef struct {
  gchar *name;
  gchar *version;
  gint gfx_width;
  gint gfx_height;
  GHashTable *colors;        // Every key of the [color] group, to its 0xRRGGBB.
  guint32 background_color;  // The value of the required key background.
  GHashTable *globals;  // Every key of the [global] group, to a BosonogaValue.
} BosonogaSettings;

void bosonoga_settings_clear(BosonogaSettings *settings);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(BosonogaSettings, bosonoga_settings_clear)

// Returns whether the [color] group has the key, and stores its value in color.
gboolean bosonoga_settings_color(const BosonogaSettings *settings,
                                 const gchar *key, guint32 *color);

// Returns whether the [global] group, which is optional, has the key, and
// stores its value in value.
gboolean bosonoga_settings_global(const BosonogaSettings *settings,
                                  const gchar *key, BosonogaValue *value);

// Reads the settings file at the path, which is BOSONOGA_SETTINGS_FILE in the
// directory of a project by convention, into settings, which must be zeroed and
// is cleared by the caller, also on failure.
gboolean bosonoga_read_settings(const gchar *path, BosonogaSettings *settings,
                                GError **error);

#endif  // BOSONOGA_SETTINGS_H_
