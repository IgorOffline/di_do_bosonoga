#ifndef BOSONOGA_GFX_H_
#define BOSONOGA_GFX_H_

#include <bosonoga/parser.h>
#include <bosonoga/settings.h>
#include <glib.h>

// Runs the program from bosonoga_parse in a window until gfx_main returns, see
// parser.h, and prints what its info statements print to stdout. The window has
// the size and colors of the settings, and draws 60 frames per second if the
// display can show that many. Returns whether there was no error, and the
// status of the return statement is stored in exit_status. Closing the window
// before that is an error.
gboolean bosonoga_gfx_run(const BosonogaProgram *program,
                          const BosonogaSettings *settings, gint *exit_status,
                          GError **error);

#endif  // BOSONOGA_GFX_H_
