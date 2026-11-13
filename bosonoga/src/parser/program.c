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

// After io_raw_import or mod, reads the tokens of the file until its end. Those
// after the file name follow those of a raw import, as if it were written
// there, and are read again after the items of a module.
static gboolean bosonoga_parse_file(BosonogaParser *parser, gboolean is_module,
                                    GError **error) {
  const gchar *keyword = is_module ? "mod" : "io_raw_import";
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  const BosonogaToken name = parser->tokenizer.token;
  if (name.kind != BOSONOGA_TOKEN_LITERAL ||
      name.value.kind != BOSONOGA_VALUE_STR) {
    g_autofree gchar *expected = g_strdup_printf(
        "a file name, as in %s \"lorem.bosonoga\"_str", keyword);
    return bosonoga_parse_error(parser, expected, error);
  }
  if (!bosonoga_is_import_name(name.value.str.text, name.value.str.length)) {
    return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_SYNTAX,
                            "%s takes the name of a .bosonoga file in the "
                            "directory of the program, as in %s "
                            "\"lorem.bosonoga\"_str",
                            keyword, keyword);
  }
  g_autofree gchar *file_name =
      g_strndup(name.value.str.text, name.value.str.length);
  g_autofree gchar *program_name = g_path_get_basename(parser->program->path);
  if (g_str_equal(file_name, program_name)) {
    return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_SYNTAX,
                            "a program cannot read itself with %s", keyword);
  }
  GArray *imports = parser->program->imports;
  for (guint i = 0; i < imports->len; ++i) {
    g_autofree gchar *imported = g_path_get_basename(
        g_array_index(imports, BosonogaProgramImport, i).path);
    if (g_str_equal(file_name, imported)) {
      return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_SYNTAX,
                              "%s is already read, with mod or io_raw_import",
                              file_name);
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
                            "cannot read %s: %s", path, read_error->message);
  }
  const BosonogaProgramImport import = {g_steal_pointer(&path), source};
  g_array_append_val(imports, import);

  const BosonogaOuterFile outer = {parser->tokenizer, is_module, parser->scope};
  g_array_append_val(parser->outer_files, outer);
  if (is_module) {
    const BosonogaScope scope = {import.path, parser->scope};
    parser->scope = parser->scopes->len;
    g_array_append_val(parser->scopes, scope);
  }
  bosonoga_tokenizer_init(&parser->tokenizer, import.path, import.source,
                          source_length);
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  if (is_module && parser->tokenizer.token.kind == BOSONOGA_TOKEN_END) {
    return bosonoga_fail_at(parser, &name, error, BOSONOGA_ERROR_SYNTAX,
                            "%s has no items, and a module has at least one",
                            file_name);
  }
  return TRUE;
}

// At the end of the items of a module, goes on after its mod.
static gboolean bosonoga_end_module(BosonogaParser *parser, GError **error) {
  const guint last = parser->outer_files->len - 1;
  const BosonogaOuterFile outer =
      g_array_index(parser->outer_files, BosonogaOuterFile, last);
  g_array_set_size(parser->outer_files, last);
  parser->tokenizer = outer.tokenizer;
  parser->scope = outer.scope;
  return bosonoga_advance(parser, error);
}

static gboolean bosonoga_parse_top_val(BosonogaParser *parser, GError **error) {
  BosonogaToken name;
  return bosonoga_advance(parser, error) &&
         bosonoga_parse_new_name(parser, &name, error) &&
         bosonoga_expect_word(parser, "is", error) &&
         bosonoga_parse_val_array(parser, &name, error);
}

static gboolean bosonoga_parse_pub_item(BosonogaParser *parser,
                                        GError **error) {
  if (parser->scope == 0) {
    return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                            BOSONOGA_ERROR_SYNTAX,
                            "pub is only in a module, a file read with mod");
  }
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  const guint function = parser->functions->len;
  if (bosonoga_at_word(parser, "const")) {
    if (!bosonoga_parse_constant(parser, error)) {
      return FALSE;
    }
    g_array_index(parser->constants, BosonogaConstant,
                  parser->constants->len - 1)
        .declaration.is_pub = TRUE;
    return TRUE;
  }
  if (bosonoga_at_word(parser, "def")) {
    if (!bosonoga_parse_function(parser, error)) {
      return FALSE;
    }
  } else if (bosonoga_at_word(parser, "val")) {
    if (!bosonoga_parse_top_val(parser, error)) {
      return FALSE;
    }
  } else {
    return bosonoga_parse_error(parser, "const, def or val after pub", error);
  }
  g_array_index(parser->function_declarations, BosonogaDeclaration, function)
      .is_pub = TRUE;
  return TRUE;
}

// Of all that is declared, as only the variables of a block are checked at its
// end.
static gboolean bosonoga_check_all_used(const BosonogaParser *parser,
                                        GError **error) {
  for (guint i = 0; i < parser->constants->len; ++i) {
    const BosonogaConstant *constant =
        &g_array_index(parser->constants, BosonogaConstant, i);
    if (!constant->declaration.is_used) {
      return bosonoga_fail_unused(parser, &constant->declaration, "used",
                                  error);
    }
  }
  for (guint i = 0; i < parser->functions->len; ++i) {
    const BosonogaFunction *function =
        &g_array_index(parser->functions, BosonogaFunction, i);
    const BosonogaDeclaration *declaration =
        &g_array_index(parser->function_declarations, BosonogaDeclaration, i);
    if (!function->is_var && !declaration->is_used) {
      return bosonoga_fail_unused(
          parser, declaration, function->is_array ? "read" : "called", error);
    }
  }
  return bosonoga_check_variables_used(parser, 0, error);
}

static gboolean bosonoga_parse_program(BosonogaParser *parser, GError **error) {
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  gboolean has_main = FALSE;
  gboolean has_gfx_section = FALSE;
  do {
    if (parser->tokenizer.token.kind == BOSONOGA_TOKEN_END &&
        parser->outer_files->len > 0) {
      if (!bosonoga_end_module(parser, error)) {
        return FALSE;
      }
    } else if (bosonoga_at_word(parser, "io_raw_import") ||
               bosonoga_at_word(parser, "mod")) {
      if (!bosonoga_parse_file(parser, bosonoga_at_word(parser, "mod"),
                               error)) {
        return FALSE;
      }
    } else if (bosonoga_at_word(parser, "pub")) {
      if (!bosonoga_parse_pub_item(parser, error)) {
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
      if (!bosonoga_parse_top_val(parser, error)) {
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
  } while (parser->tokenizer.token.kind != BOSONOGA_TOKEN_END ||
           parser->outer_files->len > 0);

  if (!parser->has_section[BOSONOGA_SECTION_MAIN]) {
    return bosonoga_fail_at(
        parser, &parser->tokenizer.token, error, BOSONOGA_ERROR_SYNTAX,
        has_gfx_section ? "a gfx program needs a gfx_main block"
                        : "a program needs a bosonoga_main block");
  }

  if (!bosonoga_check_all_used(parser, error)) {
    return FALSE;
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

  g_autoptr(GArray) outer_files =
      g_array_new(FALSE, FALSE, sizeof(BosonogaOuterFile));
  g_autoptr(GArray) scopes = g_array_new(FALSE, FALSE, sizeof(BosonogaScope));
  const BosonogaScope program_scope = {path, 0};
  g_array_append_val(scopes, program_scope);
  g_autoptr(GArray) function_declarations =
      g_array_new(FALSE, FALSE, sizeof(BosonogaDeclaration));
  BosonogaParser parser;
  memset(&parser, 0, sizeof(parser));
  bosonoga_tokenizer_init(&parser.tokenizer, path, source, length);
  parser.outer_files = outer_files;
  parser.scopes = scopes;
  parser.function_declarations = function_declarations;
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
