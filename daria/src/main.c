#include <glib.h>
#include <locale.h>

#include "daria.h"
#include "interpreter.h"
#include "parser.h"

#define DARIA_DEFAULT_PATH "input/main.daria"

static gboolean daria_run_file(const gchar *path, GError **error) {
  g_autofree gchar *source = NULL;
  gsize source_length = 0;
  if (!g_file_get_contents(path, &source, &source_length, error)) {
    return FALSE;
  }

  g_autofree gchar *display_path = g_filename_display_name(path);
  g_autoptr(GArray) program =
      daria_parse(display_path, source, source_length, error);
  return program != NULL && daria_run(program, error);
}

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");

  g_autoptr(GError) error = NULL;
  g_autoptr(GOptionContext) context = g_option_context_new("[file.daria]");
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    return DARIA_EXIT_FAILURE;
  }
  if (argc > 2) {
    g_printerr("usage: %s [file.daria]\n", g_get_prgname());
    return DARIA_EXIT_FAILURE;
  }

  if (!daria_run_file(argc == 2 ? argv[1] : DARIA_DEFAULT_PATH, &error)) {
    g_printerr("%s\n", error->message);
    return DARIA_EXIT_FAILURE;
  }
  return DARIA_EXIT_SUCCESS;
}
