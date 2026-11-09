#ifndef BOSONOGA_TEST_RUNNER_H_
#define BOSONOGA_TEST_RUNNER_H_

#include <bosonoga/settings.h>
#include <glib.h>

// Returns whether the test at the path passes. The path of a test that fails is
// printed to stderr, but why it failed is a debug message, which is shown when
// running with G_MESSAGES_DEBUG=all.
gboolean bosonoga_test_passes(const gchar *path,
                              const BosonogaSettings *settings);

// Runs every test in the directory, which is every file with the suffix
// .bosonoga, in the order of their names compared byte by byte, so that it is
// the same on every system, and adds to passed and total. A prefix of digits
// with leading zeros, like 01_ and 02_, orders the tests. A test can have a
// file with its name and the suffix .expected, which has exactly what the test
// must print.
gboolean bosonoga_run_test_directory(const gchar *directory,
                                     const BosonogaSettings *settings,
                                     guint *passed, guint *total,
                                     GError **error);

#endif  // BOSONOGA_TEST_RUNNER_H_
