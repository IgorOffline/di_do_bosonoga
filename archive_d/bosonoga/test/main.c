#include <bosonoga/bosonoga.h>
#include <bosonoga/settings.h>
#include <bosonoga/test_runner.h>
#include <glib.h>
#include <locale.h>

#define BOSONOGA_TEST_DIRECTORY "bosonoga/test"

static const gchar *const bosonoga_test_tiers[] = {
    "silver",
    "gold",
    "platinum",
};

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

  g_auto(BosonogaSettings) settings = {0};
  if (!bosonoga_read_settings(BOSONOGA_SETTINGS_FILE, &settings, &error)) {
    g_printerr("%s\n", error->message);
    return BOSONOGA_EXIT_FAILURE;
  }

  guint passed = 0;
  guint total = 0;
  if (argc == 2) {
    total = 1;
    passed = bosonoga_test_passes(argv[1], &settings) ? 1 : 0;
  } else {
    for (gsize i = 0; i < G_N_ELEMENTS(bosonoga_test_tiers); ++i) {
      g_autofree gchar *directory = g_build_filename(
          BOSONOGA_TEST_DIRECTORY, bosonoga_test_tiers[i], NULL);
      guint tier_passed = 0;
      guint tier_total = 0;
      if (!bosonoga_run_test_directory(directory, &settings, &tier_passed,
                                       &tier_total, &error)) {
        g_printerr("%s\n", error->message);
        return BOSONOGA_EXIT_FAILURE;
      }
      g_print("%s: %u/%u\n", bosonoga_test_tiers[i], tier_passed, tier_total);
      passed += tier_passed;
      total += tier_total;
    }
  }

  g_print("%u/%u\n", passed, total);
  return total > 0 && passed == total ? BOSONOGA_EXIT_SUCCESS
                                      : BOSONOGA_EXIT_FAILURE;
}
