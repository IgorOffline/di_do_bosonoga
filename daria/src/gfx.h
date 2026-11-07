#ifndef DARIA_GFX_H_
#define DARIA_GFX_H_

#include <glib.h>

#include "parser/parser.h"
#include "settings.h"

// Runs the program from daria_parse in a window until gfx_main returns, see
// parser.h, and prints what its info statements print to stdout. The window has
// the size and colors of the settings, and draws 60 frames per second if the
// display can show that many. Returns whether there was no error, and the
// status of the return statement is stored in exit_status. Closing the window
// before that is an error.
gboolean daria_gfx_run(const DariaProgram *program,
                       const DariaSettings *settings, gint *exit_status,
                       GError **error);

#endif  // DARIA_GFX_H_
