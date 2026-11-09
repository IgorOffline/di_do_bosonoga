#include <bosonoga/bosonoga.h>
#include <bosonoga/gfx.h>
#include <bosonoga/interpreter.h>
#include <bosonoga/parser.h>
#include <bosonoga/settings.h>
#include <glib.h>
#include <locale.h>

// The program of daria, which is run unless the path of another is given.
#define DARIA_MAIN_PATH "main.bosonoga"

// Runs the program at the path as a program, not as a test, in a window if it
// is a gfx program, so its exit status is returned as is.
static gboolean daria_run_program(const gchar *path,
                                  const BosonogaSettings *settings,
                                  gint *exit_status, GError **error) {
  g_autofree gchar *source = NULL;
  gsize source_length = 0;
  if (!g_file_get_contents(path, &source, &source_length, error)) {
    return FALSE;
  }
  g_autofree gchar *display_path = g_filename_display_name(path);
  g_autoptr(BosonogaProgram) program =
      bosonoga_parse(display_path, source, source_length, settings, error);
  if (program == NULL) {
    return FALSE;
  }
  if (program->is_gfx) {
    return bosonoga_gfx_run(program, settings, exit_status, error);
  }
  return bosonoga_run(program, NULL, exit_status, error);
}

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");

  g_autoptr(GError) error = NULL;
  g_autoptr(GOptionContext) context = g_option_context_new("[file.bosonoga]");
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    return BOSONOGA_EXIT_FAILURE;
  }
  if (argc > 2) {
    g_printerr("usage: %s [file.bosonoga]\n", g_get_prgname());
    return BOSONOGA_EXIT_FAILURE;
  }

  // Programs can name the colors and globals of the settings.
  g_auto(BosonogaSettings) settings = {0};
  if (!bosonoga_read_settings(BOSONOGA_SETTINGS_FILE, &settings, &error)) {
    g_printerr("%s\n", error->message);
    return BOSONOGA_EXIT_FAILURE;
  }
  g_print("%s %s\n", settings.name, settings.version);

  gint exit_status = BOSONOGA_EXIT_FAILURE;
  if (!daria_run_program(argc == 2 ? argv[1] : DARIA_MAIN_PATH, &settings,
                         &exit_status, &error)) {
    g_printerr("%s\n", error->message);
    return BOSONOGA_EXIT_FAILURE;
  }
  return exit_status;
}
