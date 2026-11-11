#ifndef BOSONOGA_KEY_H_
#define BOSONOGA_KEY_H_

#include <glib.h>

// A key name is as miniquad names it, like W, Key0 or Escape, so that the
// keys of corela can be copied. Its code is that of sokol_app, so that a
// window passes it on as it is.
gboolean bosonoga_key_named(const gchar *name, guint32 *code);

// The name of a code, or "?" if it has none.
const gchar *bosonoga_key_name(guint32 code);

#endif  // BOSONOGA_KEY_H_
