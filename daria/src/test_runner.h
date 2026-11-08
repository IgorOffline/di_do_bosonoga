#ifndef DARIA_TEST_RUNNER_H_
#define DARIA_TEST_RUNNER_H_

#include <glib.h>

#include "settings.h"

// Returns whether the test at the path passes. The path of a test that fails is
// printed to stderr, but why it failed is a debug message, which is shown when
// running with G_MESSAGES_DEBUG=all.
gboolean daria_test_passes(const gchar *path, const DariaSettings *settings);

// Runs every test in the test directory, and adds to passed and total.
gboolean daria_run_test_directory(const DariaSettings *settings, guint *passed,
                                  guint *total, GError **error);

#endif  // DARIA_TEST_RUNNER_H_
