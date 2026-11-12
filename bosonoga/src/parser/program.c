#include <string.h>

#include "parser/internal.h"

static gboolean bosonoga_parse_section(BosonogaParser *parser,
                                       BosonogaSection section,
                                       const gchar *word, GError **error) {
  if (parser->has_section[section]) {
    return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                            BOSONOGA_ERROR_SYNTAX,
                            "there is already a %s block", word);
  }
  parser->has_section[section] = TRUE;
  parser->program->entries[section] = parser->ops->len;
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }

  parser->section = section;
  if (section == BOSONOGA_SECTION_GFX_FRAME) {
    parser->delta_time_slot = parser->slot_count++;
    bosonoga_emit(parser, BOSONOGA_OP_GFX_DELTA_TIME, &parser->tokenizer.token)
        ->slot = parser->delta_time_slot;
  }
  if (section == BOSONOGA_SECTION_GFX_KEY_DOWN) {
    parser->key_slot = parser->slot_count++;
    bosonoga_emit(parser, BOSONOGA_OP_GFX_KEY, &parser->tokenizer.token)->slot =
        parser->key_slot;
  }
  if (!bosonoga_parse_block(parser, error)) {
    return FALSE;
  }
  bosonoga_emit(parser,
                section == BOSONOGA_SECTION_MAIN ? BOSONOGA_OP_MISSING_RETURN
                                                 : BOSONOGA_OP_SECTION_END,
                &parser->tokenizer.token);
  return TRUE;
}

static gboolean bosonoga_parse_gfx_section(BosonogaParser *parser,
                                           const gchar *what, GError **error) {
  static const struct {
    const gchar *word;
    BosonogaSection section;
  } sections[] = {
      {"gfx_init", BOSONOGA_SECTION_GFX_INIT},
      {"gfx_frame", BOSONOGA_SECTION_GFX_FRAME},
      {"gfx_cleanup", BOSONOGA_SECTION_GFX_CLEANUP},
      {"gfx_key_down", BOSONOGA_SECTION_GFX_KEY_DOWN},
      {"gfx_main", BOSONOGA_SECTION_MAIN},
  };
  gsize i = 0;
  while (i < G_N_ELEMENTS(sections) &&
         !bosonoga_at_word(parser, sections[i].word)) {
    ++i;
  }
  if (i == G_N_ELEMENTS(sections)) {
    return bosonoga_parse_error(parser, what, error);
  }
  parser->program->is_gfx = TRUE;
  return bosonoga_parse_section(parser, sections[i].section, sections[i].word,
                                error);
}

#define BOSONOGA_FILE_SUFFIX ".bosonoga"
#define BOSONOGA_EXPECTED_FILE_SUFFIX ".xp.bosonoga"

static gboolean bosonoga_text_ends_with(const gchar *text, gsize length,
                                        const gchar *suffix) {
  const gsize suffix_length = strlen(suffix);
  return length >= suffix_length &&
         memcmp(text + length - suffix_length, suffix, suffix_length) == 0;
}

// A file of the directory, named like lorem.bosonoga, and not expected output.
static gboolean bosonoga_is_import_name(const gchar *text, gsize length) {
  if (length <= strlen(BOSONOGA_FILE_SUFFIX) || text[0] == '.' ||
      !g_utf8_validate(text, (gssize)length, NULL) ||
      !bosonoga_text_ends_with(text, length, BOSONOGA_FILE_SUFFIX) ||
      bosonoga_text_ends_with(text, length, BOSONOGA_EXPECTED_FILE_SUFFIX)) {
    return FALSE;
  }
  for (gsize i = 0; i < length; ++i) {
    if (text[i] == '/' || text[i] == '\\') {
      return FALSE;
    }
  }
  return TRUE;
}

// After io_raw_import, reads the tokens of the file until its end, and then
// those after the file name.
static gboolean bosonoga_parse_import(BosonogaParser *parser, GError **error) {
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  const BosonogaToken name = parser->tokenizer.token;
  if (name.kind != BOSONOGA_TOKEN_LITERAL ||
      name.value.kind != BOSONOGA_VALUE_STR) {
    return bosonoga_parse_error(
        parser, "a file name, as in io_raw_import \"lorem.bosonoga\"_str",
        error);
  }
  if (!bosonoga_is_import_name(name.value.str.text, name.value.str.length)) {
    return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_SYNTAX,
                            "io_raw_import takes the name of a .bosonoga file "
                            "in the directory of the program, as in "
                            "io_raw_import \"lorem.bosonoga\"_str");
  }
  g_autofree gchar *file_name =
      g_strndup(name.value.str.text, name.value.str.length);
  g_autofree gchar *program_name = g_path_get_basename(parser->program->path);
  if (g_str_equal(file_name, program_name)) {
    return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_SYNTAX,
                            "a program cannot import itself");
  }
  GArray *imports = parser->program->imports;
  for (guint i = 0; i < imports->len; ++i) {
    g_autofree gchar *imported = g_path_get_basename(
        g_array_index(imports, BosonogaProgramImport, i).path);
    if (g_str_equal(file_name, imported)) {
      return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_SYNTAX,
                              "%s is already imported", file_name);
    }
  }

  g_autofree gchar *directory = g_path_get_dirname(parser->program->path);
  g_autofree gchar *path = g_str_equal(directory, ".")
                               ? g_strdup(file_name)
                               : g_build_filename(directory, file_name, NULL);
  g_autoptr(GError) read_error = NULL;
  g_autofree gchar *system_path =
      g_filename_from_utf8(path, -1, NULL, NULL, &read_error);
  gchar *source = NULL;
  gsize source_length = 0;
  if (system_path == NULL ||
      !g_file_get_contents(system_path, &source, &source_length, &read_error)) {
    return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_SYNTAX,
                            "cannot import %s: %s", path, read_error->message);
  }
  const BosonogaProgramImport import = {g_steal_pointer(&path), source};
  g_array_append_val(imports, import);

  g_array_append_val(parser->outer_tokenizers, parser->tokenizer);
  bosonoga_tokenizer_init(&parser->tokenizer, import.path, import.source,
                          source_length);
  return bosonoga_advance(parser, error);
}

static gboolean bosonoga_parse_program(BosonogaParser *parser, GError **error) {
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  gboolean has_main = FALSE;
  gboolean has_gfx_section = FALSE;
  do {
    if (bosonoga_at_word(parser, "io_raw_import")) {
      if (!bosonoga_parse_import(parser, error)) {
        return FALSE;
      }
    } else if (bosonoga_at_word(parser, "const")) {
      if (!bosonoga_parse_constant(parser, error)) {
        return FALSE;
      }
    } else if (bosonoga_at_word(parser, "def")) {
      if (!bosonoga_parse_function(parser, error)) {
        return FALSE;
      }
    } else if (bosonoga_at_word(parser, "val")) {
      BosonogaToken name;
      if (!bosonoga_advance(parser, error) ||
          !bosonoga_parse_new_name(parser, &name, error) ||
          !bosonoga_expect_word(parser, "is", error) ||
          !bosonoga_parse_val_array(parser, &name, error)) {
        return FALSE;
      }
    } else if (bosonoga_at_word(parser, "array")) {
      return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                              BOSONOGA_ERROR_SYNTAX,
                              "an array is declared as val NAME is array, as "
                              "in val task is array 0_i32");
    } else if (bosonoga_at_word(parser, "var")) {
      return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                              BOSONOGA_ERROR_SYNTAX,
                              "a var is a variable of a block, and a "
                              "top-level array is a val");
    } else if (bosonoga_at_word(parser, "bosonoga_global")) {
      if (has_main || has_gfx_section ||
          parser->has_section[BOSONOGA_SECTION_GLOBAL]) {
        return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                                BOSONOGA_ERROR_SYNTAX,
                                "bosonoga_global must be the first section");
      }
      if (!bosonoga_parse_section(parser, BOSONOGA_SECTION_GLOBAL,
                                  "bosonoga_global", error)) {
        return FALSE;
      }
    } else if (bosonoga_at_word(parser, "bosonoga_main") && !has_gfx_section &&
               !has_main) {
      has_main = TRUE;
      if (!bosonoga_parse_section(parser, BOSONOGA_SECTION_MAIN,
                                  "bosonoga_main", error)) {
        return FALSE;
      }
    } else if (has_main) {
      return bosonoga_parse_error(parser, "const, def, val or end of file",
                                  error);
    } else {
      const gchar *what =
          has_gfx_section ? "const, def, val, gfx_init, gfx_frame, "
                            "gfx_cleanup, gfx_key_down or gfx_main"
          : parser->has_section[BOSONOGA_SECTION_GLOBAL]
              ? "const, def, val, bosonoga_main, gfx_init, gfx_frame, "
                "gfx_cleanup, gfx_key_down or gfx_main"
              : "const, def, val, bosonoga_global, bosonoga_main, gfx_init, "
                "gfx_frame, gfx_cleanup, gfx_key_down or gfx_main";
      if (!bosonoga_parse_gfx_section(parser, what, error)) {
        return FALSE;
      }
      has_gfx_section = TRUE;
    }
  } while (parser->tokenizer.token.kind != BOSONOGA_TOKEN_END);

  if (!parser->has_section[BOSONOGA_SECTION_MAIN]) {
    return bosonoga_fail_at(
        parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_SYNTAX,
        has_gfx_section ? "a gfx program needs a gfx_main block"
                        : "a program needs a bosonoga_main block");
  }

  for (guint section = 0; section < BOSONOGA_SECTION_COUNT; ++section) {
    if (!parser->has_section[section]) {
      parser->program->entries[section] = parser->ops->len;
      bosonoga_emit(parser, BOSONOGA_OP_SECTION_END, &parser->tokenizer.token);
    }
  }
  return TRUE;
}

static void bosonoga_program_asset_clear(gpointer data) {
  BosonogaProgramAsset *asset = data;
  g_free(asset->key);
  g_free(asset->text);
  g_free(asset->path);
}

static void bosonoga_program_import_clear(gpointer data) {
  BosonogaProgramImport *import = data;
  g_free(import->path);
  g_free(import->source);
}

void bosonoga_program_free(BosonogaProgram *program) {
  g_array_unref(program->imports);
  g_array_unref(program->ops);
  g_array_unref(program->functions);
  g_array_unref(program->rows);
  g_array_unref(program->fonts);
  g_array_unref(program->icons);
  g_free(program);
}

BosonogaProgram *bosonoga_parse(const gchar *path, const gchar *source,
                                gsize length, const BosonogaSettings *settings,
                                GError **error) {
  g_autoptr(GArray) variables =
      g_array_new(FALSE, FALSE, sizeof(BosonogaVariable));
  g_autoptr(GArray) constants =
      g_array_new(FALSE, FALSE, sizeof(BosonogaConstant));
  // A loop that a failed parse leaves open is freed with the array.
  g_autoptr(GArray) loops = g_array_new(FALSE, FALSE, sizeof(BosonogaLoop));
  g_array_set_clear_func(loops, bosonoga_loop_clear);
  g_autoptr(BosonogaProgram) program = g_new0(BosonogaProgram, 1);
  program->path = path;
  program->imports = g_array_new(FALSE, FALSE, sizeof(BosonogaProgramImport));
  g_array_set_clear_func(program->imports, bosonoga_program_import_clear);
  program->ops = g_array_new(FALSE, TRUE, sizeof(BosonogaOp));
  program->functions = g_array_new(FALSE, FALSE, sizeof(BosonogaFunction));
  program->rows = g_array_new(FALSE, FALSE, sizeof(BosonogaValue));
  program->fonts = g_array_new(FALSE, FALSE, sizeof(BosonogaProgramAsset));
  g_array_set_clear_func(program->fonts, bosonoga_program_asset_clear);
  program->icons = g_array_new(FALSE, FALSE, sizeof(BosonogaProgramAsset));
  g_array_set_clear_func(program->icons, bosonoga_program_asset_clear);

  g_autoptr(GArray) outer_tokenizers =
      g_array_new(FALSE, FALSE, sizeof(BosonogaTokenizer));
  BosonogaParser parser;
  memset(&parser, 0, sizeof(parser));
  bosonoga_tokenizer_init(&parser.tokenizer, path, source, length);
  parser.outer_tokenizers = outer_tokenizers;
  parser.ops = program->ops;
  parser.variables = variables;
  parser.constants = constants;
  parser.loops = loops;
  parser.functions = program->functions;
  parser.program = program;
  parser.settings = settings;
  if (!bosonoga_parse_program(&parser, error)) {
    return NULL;
  }
  program->slot_count = parser.slot_count;
  return g_steal_pointer(&program);
}
