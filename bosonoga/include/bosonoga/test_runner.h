#ifndef BOSONOGA_TEST_RUNNER_H_
#define BOSONOGA_TEST_RUNNER_H_

#include <bosonoga/settings.h>
#include <glib.h>

gboolean bosonoga_test_passes(const gchar *path,
                              const BosonogaSettings *settings);

// Runs every .bosonoga file in the directory, in the byte order of their names,
// and adds to passed and total. A test passes if it returns exit_success and
// prints exactly what its .expected file, if it has one, holds.
gboolean bosonoga_run_test_directory(const gchar *directory,
                                     const BosonogaSettings *settings,
                                     guint *passed, guint *total,
                                     GError **error);

#endif  // BOSONOGA_TEST_RUNNER_H_
