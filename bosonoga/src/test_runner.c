#include "bosonoga/test_runner.h"

#include <string.h>

#include "bosonoga/bosonoga.h"
#include "bosonoga/interpreter.h"
#include "bosonoga/parser.h"

#define BOSONOGA_TEST_SUFFIX ".bosonoga"
#define BOSONOGA_EXPECTED_SUFFIX ".expected"

// A test without an expected file can print anything.
static gboolean bosonoga_check_output(const gchar *path,
                                      const gchar *display_path,
                                      const GString *output, GError **error) {
  const gsize stem_length = g_str_has_suffix(path, BOSONOGA_TEST_SUFFIX)
                                ? strlen(path) - strlen(BOSONOGA_TEST_SUFFIX)
                                : strlen(path);
  g_autofree gchar *stem = g_strndup(path, stem_length);
  g_autofree gchar *expected_path =
      g_strconcat(stem, BOSONOGA_EXPECTED_SUFFIX, NULL);
  if (!g_file_test(expected_path, G_FILE_TEST_EXISTS)) {
    return TRUE;
  }

  g_autofree gchar *expected = NULL;
  if (!g_file_get_contents(expected_path, &expected, NULL, error)) {
    return FALSE;
  }
  if (!g_str_equal(expected, output->str)) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_TEST,
                "%s: printed '%s', but expected '%s'", display_path,
                output->str, expected);
    return FALSE;
  }
  return TRUE;
}

static gboolean bosonoga_run_test(const gchar *path,
                                  const BosonogaSettings *settings,
                                  GError **error) {
  g_autofree gchar *source = NULL;
  gsize source_length = 0;
  if (!g_file_get_contents(path, &source, &source_length, error)) {
    return FALSE;
  }

  g_autofree gchar *display_path = g_filename_display_name(path);
  g_autoptr(BosonogaProgram) program =
      bosonoga_parse(display_path, source, source_length, settings, error);
  g_autoptr(GString) output = g_string_new(NULL);
  gint exit_status = BOSONOGA_EXIT_FAILURE;
  if (program == NULL || !bosonoga_run(program, output, &exit_status, error)) {
    return FALSE;
  }
  if (exit_status != BOSONOGA_EXIT_SUCCESS) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_TEST,
                "%s: returned exit_failure", display_path);
    return FALSE;
  }
  return bosonoga_check_output(path, display_path, output, error);
}

gboolean bosonoga_test_passes(const gchar *path,
                              const BosonogaSettings *settings) {
  g_autoptr(GError) error = NULL;
  if (!bosonoga_run_test(path, settings, &error)) {
    g_autofree gchar *display_path = g_filename_display_name(path);
    g_printerr("FAIL %s\n", display_path);
    g_debug("FAIL %s", error->message);
    return FALSE;
  }
  return TRUE;
}

// Unlike g_dir_read_name and g_utf8_collate, the same on every system.
static gint bosonoga_compare_names(gconstpointer left, gconstpointer right) {
  return strcmp(left, right);
}

gboolean bosonoga_run_test_directory(const gchar *directory,
                                     const BosonogaSettings *settings,
                                     guint *passed, guint *total,
                                     GError **error) {
  g_autoptr(GDir) entries = g_dir_open(directory, 0, error);
  if (entries == NULL) {
    return FALSE;
  }

  g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func(g_free);
  const gchar *name = NULL;
  while ((name = g_dir_read_name(entries)) != NULL) {
    if (g_str_has_suffix(name, BOSONOGA_TEST_SUFFIX)) {
      g_ptr_array_add(names, g_strdup(name));
    }
  }
  g_ptr_array_sort_values(names, bosonoga_compare_names);

  for (guint i = 0; i < names->len; ++i) {
    g_autofree gchar *path =
        g_build_filename(directory, g_ptr_array_index(names, i), NULL);
    ++*total;
    if (bosonoga_test_passes(path, settings)) {
      ++*passed;
    }
  }
  return TRUE;
}
