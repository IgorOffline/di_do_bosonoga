#include <glib.h>
#include <locale.h>
#include <string.h>

#include "daria.h"
#include "gfx.h"
#include "interpreter.h"
#include "parser.h"

#define DARIA_TEST_DIRECTORY "test"
#define DARIA_TEST_SUFFIX ".bosonoga"
#define DARIA_EXPECTED_SUFFIX ".expected"
#define DARIA_MAIN_PATH "main.bosonoga"
#define DARIA_SETTINGS_PATH "regina.ini"
#define DARIA_SETTINGS_GROUP_META "meta"
#define DARIA_SETTINGS_GROUP_GFX "gfx"

typedef struct {
  gchar *name;
  gchar *version;
  gint gfx_width;
  gint gfx_height;
} DariaSettings;

static void daria_settings_clear(DariaSettings *settings) {
  g_free(settings->name);
  g_free(settings->version);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(DariaSettings, daria_settings_clear)

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

// Returns a new string with the value of the key. GKeyFile does not remove the
// quotes around a value, so that is done here.
static gchar *daria_read_text_setting(GKeyFile *key_file, const gchar *key,
                                      GError **error) {
  GError *read_error = NULL;
  g_autofree gchar *value = g_key_file_get_string(
      key_file, DARIA_SETTINGS_GROUP_META, key, &read_error);
  if (value == NULL) {
    g_propagate_prefixed_error(error, read_error, "%s: ", DARIA_SETTINGS_PATH);
    return NULL;
  }
  const gsize length = strlen(value);
  if (length >= 2 && value[0] == '"' && value[length - 1] == '"') {
    return g_strndup(value + 1, length - 2);
  }
  return g_steal_pointer(&value);
}

static gboolean daria_read_positive_setting(GKeyFile *key_file,
                                            const gchar *key, gint *value,
                                            GError **error) {
  GError *read_error = NULL;
  *value = g_key_file_get_integer(key_file, DARIA_SETTINGS_GROUP_GFX, key,
                                  &read_error);
  if (read_error != NULL) {
    g_propagate_prefixed_error(error, read_error, "%s: ", DARIA_SETTINGS_PATH);
    return FALSE;
  }
  if (*value <= 0) {
    g_set_error(error, DARIA_ERROR, DARIA_ERROR_RUNTIME,
                "%s: %s must be positive", DARIA_SETTINGS_PATH, key);
    return FALSE;
  }
  return TRUE;
}

static gboolean daria_read_settings(DariaSettings *settings, GError **error) {
  g_autoptr(GKeyFile) key_file = g_key_file_new();
  GError *load_error = NULL;
  if (!g_key_file_load_from_file(key_file, DARIA_SETTINGS_PATH, G_KEY_FILE_NONE,
                                 &load_error)) {
    g_propagate_prefixed_error(error, load_error, "%s: ", DARIA_SETTINGS_PATH);
    return FALSE;
  }
  settings->name = daria_read_text_setting(key_file, "name", error);
  if (settings->name == NULL) {
    return FALSE;
  }
  settings->version = daria_read_text_setting(key_file, "version", error);
  if (settings->version == NULL) {
    return FALSE;
  }
  return daria_read_positive_setting(key_file, "width", &settings->gfx_width,
                                     error) &&
         daria_read_positive_setting(key_file, "height", &settings->gfx_height,
                                     error);
}

// Runs main.bosonoga as a program, not as a test, so its exit status is
// returned as is.
static gboolean daria_run_main_program(const DariaSettings *settings,
                                       gint *exit_status, GError **error) {
  g_autofree gchar *source = NULL;
  gsize source_length = 0;
  if (!g_file_get_contents(DARIA_MAIN_PATH, &source, &source_length, error)) {
    return FALSE;
  }
  g_autoptr(DariaProgram) program =
      daria_parse(DARIA_MAIN_PATH, source, source_length, error);
  if (program == NULL) {
    return FALSE;
  }
  if (program->is_gfx) {
    return daria_gfx_run(program, settings->gfx_width, settings->gfx_height,
                         exit_status, error);
  }
  return daria_run(program, NULL, exit_status, error);
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
  if (total == 0 || passed != total) {
    return DARIA_EXIT_FAILURE;
  }
  if (argc == 2) {
    return DARIA_EXIT_SUCCESS;
  }

  g_auto(DariaSettings) settings = {NULL, NULL, 0, 0};
  if (!daria_read_settings(&settings, &error)) {
    g_printerr("%s\n", error->message);
    return DARIA_EXIT_FAILURE;
  }
  g_print("%s %s\n", settings.name, settings.version);

  gint exit_status = DARIA_EXIT_FAILURE;
  if (!daria_run_main_program(&settings, &exit_status, &error)) {
    g_printerr("%s\n", error->message);
    return DARIA_EXIT_FAILURE;
  }
  return exit_status;
}
