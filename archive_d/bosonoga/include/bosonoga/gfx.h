#ifndef BOSONOGA_GFX_H_
#define BOSONOGA_GFX_H_

#include <bosonoga/parser.h>
#include <bosonoga/settings.h>
#include <glib.h>

// Runs the program in a window until gfx_main returns, see parser.h. Closing
// the window before that is an error.
gboolean bosonoga_gfx_run(const BosonogaProgram *program,
                          const BosonogaSettings *settings, gint *exit_status,
                          GError **error);

#endif  // BOSONOGA_GFX_H_
