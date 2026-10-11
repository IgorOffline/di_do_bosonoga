#include "bosonoga/bosonoga.h"

#define BOSONOGA_TEST_DIRECTORY "test"
#define BOSONOGA_HEADER_FILE "header.bosonoga"
#define BOSONOGA_REJECT_FILE "reject.bosonoga"
#define BOSONOGA_FILE_EXTENSION ".bosonoga"
#define BOSONOGA_CASE_SEPARATOR "-----"

static const gchar* const valid_names[] = {
    "compare", "branch", "arithmetic", "f32", "loop", "shift",
};

static gboolean read_test_source(const gchar* file_name,
                                 gchar destination[BOSONOGA_SOURCE_LENGTH + 1],
                                 GError** error) {
  g_autofree gchar* path =
      g_build_filename(BOSONOGA_TEST_DIRECTORY, file_name, NULL);
  return bosonoga_read_source(path, destination, error);
}

static GStrv read_reject_cases(GError** error) {
  g_autofree gchar* path =
      g_build_filename(BOSONOGA_TEST_DIRECTORY, BOSONOGA_REJECT_FILE, NULL);
  g_autofree gchar* contents = NULL;
  if (!g_file_get_contents(path, &contents, NULL, error)) {
    return NULL;
  }
  g_autofree gchar* separator =
      g_regex_escape_string(BOSONOGA_CASE_SEPARATOR, -1);
  g_autofree gchar* pattern = g_strdup_printf("^%s\\r?(?:\\n|\\z)", separator);
  return g_regex_split_simple(pattern, contents, G_REGEX_MULTILINE, 0);
}

static gboolean run_valid(Regina* regina, const gchar* name) {
  g_autofree gchar* file_name =
      g_strconcat(name, BOSONOGA_FILE_EXTENSION, NULL);
  g_autoptr(GError) error = NULL;
  gint exit_status = BOSONOGA_EXIT_FAILURE;
  if (!read_test_source(file_name, regina->input_source, &error) ||
      !bosonoga_core(regina, regina->header_source, regina->input_source,
                     &exit_status, &error)) {
    g_printerr("FAIL %s: %s\n", file_name, error->message);
    return FALSE;
  }
  if (exit_status != BOSONOGA_EXIT_SUCCESS) {
    g_printerr("FAIL %s: returned exit_failure\n", file_name);
    return FALSE;
  }
  return TRUE;
}

static gboolean run_rejected(Regina* regina, const gchar* source,
                             guint number) {
  g_autofree gchar* stripped = g_strstrip(g_strdup(source));
  if (*stripped == '\0') {
    g_printerr("FAIL %s case %u: empty\n", BOSONOGA_REJECT_FILE, number);
    return FALSE;
  }
  g_autoptr(GError) error = NULL;
  gint exit_status = BOSONOGA_EXIT_FAILURE;
  if (bosonoga_core(regina, regina->header_source, source, &exit_status,
                    &error)) {
    g_printerr("FAIL %s case %u: was accepted\n", BOSONOGA_REJECT_FILE, number);
    return FALSE;
  }
  if (error == NULL || error->domain != BOSONOGA_ERROR) {
    g_printerr("FAIL %s case %u: %s\n", BOSONOGA_REJECT_FILE, number,
               error == NULL ? "no error was set" : error->message);
    return FALSE;
  }
  g_debug("%s case %u rejected: %s", BOSONOGA_REJECT_FILE, number,
          error->message);
  return TRUE;
}

int main(void) {
  g_autofree Regina* regina = g_new0(Regina, 1);
  g_autoptr(GError) error = NULL;
  if (!read_test_source(BOSONOGA_HEADER_FILE, regina->header_source, &error)) {
    g_printerr("%s\n", error->message);
    return BOSONOGA_EXIT_FAILURE;
  }
  g_auto(GStrv) reject_cases = read_reject_cases(&error);
  if (reject_cases == NULL) {
    g_printerr("%s\n", error->message);
    return BOSONOGA_EXIT_FAILURE;
  }

  const guint valid_total = (guint)G_N_ELEMENTS(valid_names);
  guint valid_passed = 0;
  for (guint i = 0; i < valid_total; i++) {
    if (run_valid(regina, valid_names[i])) {
      valid_passed++;
    }
  }

  const guint rejected_total = g_strv_length(reject_cases);
  guint rejected_passed = 0;
  for (guint i = 0; i < rejected_total; i++) {
    if (run_rejected(regina, reject_cases[i], i + 1)) {
      rejected_passed++;
    }
  }

  const guint passed = valid_passed + rejected_passed;
  const guint total = valid_total + rejected_total;
  g_print("GLib %u.%u.%u, passed %u/%u (valid %u/%u, rejected %u/%u)\n",
          glib_major_version, glib_minor_version, glib_micro_version, passed,
          total, valid_passed, valid_total, rejected_passed, rejected_total);
  return passed == total ? BOSONOGA_EXIT_SUCCESS : BOSONOGA_EXIT_FAILURE;
}
