#include <glib.h>
#include <locale.h>

#include "daria.h"
#include "gfx.h"
#include "interpreter.h"
#include "parser/parser.h"
#include "settings.h"
#include "test_runner.h"

#define DARIA_MAIN_PATH "main.bosonoga"

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
    return daria_gfx_run(program, settings, exit_status, error);
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

  g_auto(DariaSettings) settings = {0};
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
