#ifndef BOSONOGA_KEY_H_
#define BOSONOGA_KEY_H_

#include <glib.h>

gboolean bosonoga_key_named(const gchar *name, guint32 *code);

const gchar *bosonoga_key_name(guint32 code);

#endif  // BOSONOGA_KEY_H_
