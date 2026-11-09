#ifndef BOSONOGA_TEST_RUNNER_H_
#define BOSONOGA_TEST_RUNNER_H_

#include <bosonoga/settings.h>
#include <glib.h>

gboolean bosonoga_test_passes(const gchar *path,
                              const BosonogaSettings *settings);

// Runs every .bosonoga file in the directory, in the byte order of their names,
// and adds to passed and total. A test passes if it returns exit_success and
// prints what its .xp.bosonoga file, if it has one, expects. That file is not a
// test, and each of its lines is blank, a // comment, or "expect" and a space
// followed by one printed line, exactly, or "expect" alone for an empty one:
//
//   // A sum in columns.
//   expect   347
//   expect + 285
//   expect
gboolean bosonoga_run_test_directory(const gchar *directory,
                                     const BosonogaSettings *settings,
                                     guint *passed, guint *total,
                                     GError **error);

#endif  // BOSONOGA_TEST_RUNNER_H_
