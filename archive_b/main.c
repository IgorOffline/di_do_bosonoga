#include "bosonoga/bosonoga.h"
#include "serde/serde.h"

#define BOSONOGA_TEST_DIRECTORY "test"
#define BOSONOGA_HEADER_FILE "header.bosonoga"
#define BOSONOGA_FILE_EXTENSION ".bosonoga"

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

static void log_payload(const gchar* label, const BosonogaPayload* payload) {
  g_autoptr(GString) line = g_string_new(label);
  g_string_append(line, " i32=[");
  for (guint i = 0; i < payload->i32_values->len; i++) {
    g_string_append_printf(line, "%s%" G_GINT32_FORMAT, i == 0 ? "" : ", ",
                           g_array_index(payload->i32_values, gint32, i));
  }
  g_string_append(line, "] f32=[");
  for (guint i = 0; i < payload->f32_values->len; i++) {
    g_string_append_printf(line, "%s%.2f", i == 0 ? "" : ", ",
                           g_array_index(payload->f32_values, gdouble, i));
  }
  g_string_append(line, "]");
  g_print("%s\n", line->str);
}

static gboolean run_serde(guint* byte_count) {
  g_autoptr(GRand) rand = g_rand_new();
  g_autoptr(BosonogaPayload) sent = bosonoga_serde_random_payload(rand);
  g_autoptr(GByteArray) bytes = g_byte_array_new();
  g_autoptr(GError) error = NULL;
  log_payload("(S)", sent);
  if (!bosonoga_serde_write(sent, bytes, &error)) {
    g_printerr("FAIL serde: %s\n", error->message);
    return FALSE;
  }
  *byte_count = bytes->len;
  g_autoptr(BosonogaPayload) received = bosonoga_serde_read(bytes, &error);
  if (received == NULL) {
    g_printerr("FAIL serde: %s\n", error->message);
    return FALSE;
  }
  log_payload("(D)", received);
  if (!bosonoga_serde_equal(sent, received)) {
    g_printerr("FAIL serde: the values changed in the round trip\n");
    return FALSE;
  }
  return TRUE;
}

int main(void) {
  g_autofree Regina* regina = g_new0(Regina, 1);
  g_autoptr(GError) error = NULL;
  if (!read_test_source(BOSONOGA_HEADER_FILE, regina->header_source, &error)) {
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

  guint serde_bytes = 0;
  const guint serde_passed = run_serde(&serde_bytes) ? 1 : 0;

  const guint passed = valid_passed + serde_passed;
  const guint total = valid_total + 1;
  g_print(
      "GLib %u.%u.%u, passed %u/%u (valid %u/%u, serde %u/1, "
      "%u B)\n",
      glib_major_version, glib_minor_version, glib_micro_version, passed, total,
      valid_passed, valid_total, serde_passed, serde_bytes);
  return passed == total ? BOSONOGA_EXIT_SUCCESS : BOSONOGA_EXIT_FAILURE;
}
