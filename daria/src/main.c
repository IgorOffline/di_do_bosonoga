#include <glib.h>
#include <locale.h>
#include <string.h>

#include "daria.h"
#include "interpreter.h"
#include "parser.h"

#define DARIA_TEST_DIRECTORY "test"
#define DARIA_TEST_SUFFIX ".bosonoga"
#define DARIA_EXPECTED_SUFFIX ".expected"

// Compares output with the file that has the name of the test and the expected
// suffix. A test without that file can print anything.
static gboolean daria_check_output(const gchar *path, const gchar *display_path,
                                   const GString *output, GError **error) {
  const gsize stem_length = g_str_has_suffix(path, DARIA_TEST_SUFFIX)
                                ? strlen(path) - strlen(DARIA_TEST_SUFFIX)
                                : strlen(path);
  g_autofree gchar *stem = g_strndup(path, stem_length);
  g_autofree gchar *expected_path =
      g_strconcat(stem, DARIA_EXPECTED_SUFFIX, NULL);
  if (!g_file_test(expected_path, G_FILE_TEST_EXISTS)) {
    return TRUE;
  }

  g_autofree gchar *expected = NULL;
  if (!g_file_get_contents(expected_path, &expected, NULL, error)) {
    return FALSE;
  }
  if (!g_str_equal(expected, output->str)) {
    g_set_error(error, DARIA_ERROR, DARIA_ERROR_TEST,
                "%s: printed '%s', but expected '%s'", display_path,
                output->str, expected);
    return FALSE;
  }
  return TRUE;
}

// A test passes if it returns exit_success and prints the expected output.
static gboolean daria_run_test(const gchar *path, GError **error) {
  g_autofree gchar *source = NULL;
  gsize source_length = 0;
  if (!g_file_get_contents(path, &source, &source_length, error)) {
    return FALSE;
  }

  g_autofree gchar *display_path = g_filename_display_name(path);
  g_autoptr(DariaProgram) program =
      daria_parse(display_path, source, source_length, error);
  g_autoptr(GString) output = g_string_new(NULL);
  gint exit_status = DARIA_EXIT_FAILURE;
  if (program == NULL || !daria_run(program, output, &exit_status, error)) {
    return FALSE;
  }
  if (exit_status != DARIA_EXIT_SUCCESS) {
    g_set_error(error, DARIA_ERROR, DARIA_ERROR_TEST,
                "%s: returned exit_failure", display_path);
    return FALSE;
  }
  return daria_check_output(path, display_path, output, error);
}

// Only n/n is printed, so why a test failed is a debug message, which is shown
// when running with G_MESSAGES_DEBUG=all.
static gboolean daria_test_passes(const gchar *path) {
  g_autoptr(GError) error = NULL;
  if (!daria_run_test(path, &error)) {
    g_debug("FAIL %s", error->message);
    return FALSE;
  }
  return TRUE;
}

static gboolean daria_run_test_directory(guint *passed, guint *total,
                                         GError **error) {
  g_autoptr(GDir) directory = g_dir_open(DARIA_TEST_DIRECTORY, 0, error);
  if (directory == NULL) {
    return FALSE;
  }

  const gchar *name = NULL;
  while ((name = g_dir_read_name(directory)) != NULL) {
    if (g_str_has_suffix(name, DARIA_TEST_SUFFIX)) {
      g_autofree gchar *path =
          g_build_filename(DARIA_TEST_DIRECTORY, name, NULL);
      ++*total;
      if (daria_test_passes(path)) {
        ++*passed;
      }
    }
  }
  return TRUE;
}

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");

  g_autoptr(GError) error = NULL;
  g_autoptr(GOptionContext) context = g_option_context_new("[file.bosonoga]");
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    return DARIA_EXIT_FAILURE;
  }
  if (argc > 2) {
    g_printerr("usage: %s [file.bosonoga]\n", g_get_prgname());
    return DARIA_EXIT_FAILURE;
  }

  guint passed = 0;
  guint total = 0;
  if (argc == 2) {
    total = 1;
    passed = daria_test_passes(argv[1]) ? 1 : 0;
  } else if (!daria_run_test_directory(&passed, &total, &error)) {
    g_printerr("%s\n", error->message);
    return DARIA_EXIT_FAILURE;
  }

  g_print("%u/%u\n", passed, total);
  return total > 0 && passed == total ? DARIA_EXIT_SUCCESS : DARIA_EXIT_FAILURE;
}
