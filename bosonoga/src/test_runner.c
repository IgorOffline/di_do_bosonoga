#include "bosonoga/test_runner.h"

#include <string.h>

#include "bosonoga/bosonoga.h"
#include "bosonoga/interpreter.h"
#include "bosonoga/parser.h"
#include "tokenizer.h"

#define BOSONOGA_TEST_SUFFIX ".bosonoga"
#define BOSONOGA_EXPECTED_SUFFIX ".xp.bosonoga"
#define BOSONOGA_EXPECT_WORD "expect"
#define BOSONOGA_COMMENT_START "//"

// NULL if a line is neither blank, a comment nor an expect line.
static gchar *bosonoga_read_expected(const gchar *display_path,
                                     const gchar *text, GError **error) {
  g_autoptr(GString) expected = g_string_new(NULL);
  g_auto(GStrv) lines = g_strsplit(text, "\n", -1);
  for (gsize i = 0; lines[i] != NULL; ++i) {
    const gchar *line = lines[i];
    const gchar *start = line;
    while (*start == ' ' || *start == '\t') {
      ++start;
    }
    if (*start == '\0' || g_str_has_prefix(start, BOSONOGA_COMMENT_START)) {
      continue;
    }

    // A single space follows the word, so that the line keeps its own spaces.
    const gchar *printed = line + strlen(BOSONOGA_EXPECT_WORD);
    if (!g_str_has_prefix(line, BOSONOGA_EXPECT_WORD) ||
        (*printed != '\0' && *printed != ' ')) {
      g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_SYNTAX,
                  "%s:%" G_GSIZE_FORMAT
                  ": a line that is not a comment starts with expect",
                  display_path, i + 1);
      return NULL;
    }
    if (*printed == ' ') {
      ++printed;
    }
    g_string_append(expected, printed);
    g_string_append_c(expected, '\n');
  }
  return g_string_free_and_steal(g_steal_pointer(&expected));
}

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

  g_autofree gchar *text = NULL;
  if (!g_file_get_contents(expected_path, &text, NULL, error)) {
    return FALSE;
  }
  g_autofree gchar *expected_display_path =
      g_filename_display_name(expected_path);
  g_autofree gchar *expected =
      bosonoga_read_expected(expected_display_path, text, error);
  if (expected == NULL) {
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

// Adds the names that the file reads with io_raw_import or mod. A file that
// cannot be read or lexed adds nothing, and fails as a test.
static void bosonoga_add_imports(const gchar *path, GHashTable *imported) {
  g_autofree gchar *source = NULL;
  gsize source_length = 0;
  if (!g_file_get_contents(path, &source, &source_length, NULL)) {
    return;
  }
  BosonogaTokenizer tokenizer;
  bosonoga_tokenizer_init(&tokenizer, "", source, source_length);
  while (bosonoga_tokenizer_advance(&tokenizer, NULL) &&
         tokenizer.token.kind != BOSONOGA_TOKEN_END) {
    const BosonogaToken *token = &tokenizer.token;
    if (token->kind == BOSONOGA_TOKEN_WORD &&
        ((token->length == strlen("io_raw_import") &&
          memcmp(token->text, "io_raw_import", token->length) == 0) ||
         (token->length == strlen("mod") &&
          memcmp(token->text, "mod", token->length) == 0))) {
      if (!bosonoga_tokenizer_advance(&tokenizer, NULL)) {
        return;
      }
      if (tokenizer.token.kind != BOSONOGA_TOKEN_LITERAL ||
          tokenizer.token.value.kind != BOSONOGA_VALUE_STR) {
        continue;
      }
      g_hash_table_add(imported, g_strndup(tokenizer.token.value.str.text,
                                           tokenizer.token.value.str.length));
    }
  }
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
    if (g_str_has_suffix(name, BOSONOGA_TEST_SUFFIX) &&
        !g_str_has_suffix(name, BOSONOGA_EXPECTED_SUFFIX)) {
      g_ptr_array_add(names, g_strdup(name));
    }
  }
  g_ptr_array_sort_values(names, bosonoga_compare_names);

  g_autoptr(GHashTable) imported =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  for (guint i = 0; i < names->len; ++i) {
    g_autofree gchar *path =
        g_build_filename(directory, g_ptr_array_index(names, i), NULL);
    bosonoga_add_imports(path, imported);
  }

  for (guint i = 0; i < names->len; ++i) {
    if (g_hash_table_contains(imported, g_ptr_array_index(names, i))) {
      continue;
    }
    g_autofree gchar *path =
        g_build_filename(directory, g_ptr_array_index(names, i), NULL);
    ++*total;
    if (bosonoga_test_passes(path, settings)) {
      ++*passed;
    }
  }
  return TRUE;
}
