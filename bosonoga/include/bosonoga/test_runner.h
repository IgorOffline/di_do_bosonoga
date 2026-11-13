#ifndef BOSONOGA_TEST_RUNNER_H_
#define BOSONOGA_TEST_RUNNER_H_

#include <bosonoga/settings.h>
#include <glib.h>

gboolean bosonoga_test_passes(const gchar *path,
                              const BosonogaSettings *settings);

// In the byte order of the names. A test passes if it returns exit_success and
// prints what its .xp.bosonoga file expects, whose lines are blank, a //
// comment, or "expect" and one printed line after a single space. A file that
// another file of the directory reads with io_raw_import or mod is not a test.
gboolean bosonoga_run_test_directory(const gchar *directory,
                                     const BosonogaSettings *settings,
                                     guint *passed, guint *total,
                                     GError **error);

#endif  // BOSONOGA_TEST_RUNNER_H_
