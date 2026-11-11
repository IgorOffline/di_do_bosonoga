#ifndef BOSONOGA_SETTINGS_H_
#define BOSONOGA_SETTINGS_H_

#include <bosonoga/bosonoga.h>
#include <glib.h>

#define BOSONOGA_SETTINGS_FILE "regina.ini"

typedef struct {
  gchar *name;
  gchar *version;
  gint gfx_width;
  gint gfx_height;
  GHashTable *colors;  // Key to 0xRRGGBB.
  guint32 background_color;
  GHashTable *globals;
  GHashTable *keys;  // Key to a code of a key name, see parser.h.
} BosonogaSettings;

void bosonoga_settings_clear(BosonogaSettings *settings);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(BosonogaSettings, bosonoga_settings_clear)

gboolean bosonoga_settings_color(const BosonogaSettings *settings,
                                 const gchar *key, guint32 *color);

gboolean bosonoga_settings_key(const BosonogaSettings *settings,
                               const gchar *key, guint32 *code);

gboolean bosonoga_settings_global(const BosonogaSettings *settings,
                                  const gchar *key, BosonogaValue *value);

// Settings must be zeroed, and the caller clears them, also on failure.
gboolean bosonoga_read_settings(const gchar *path, BosonogaSettings *settings,
                                GError **error);

#endif  // BOSONOGA_SETTINGS_H_
