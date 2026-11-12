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

static gboolean bosonoga_parse_program(BosonogaParser *parser, GError **error) {
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  gboolean has_main = FALSE;
  gboolean has_gfx_section = FALSE;
  do {
    if (bosonoga_at_word(parser, "const")) {
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

static void bosonoga_program_font_clear(gpointer data) {
  BosonogaProgramFont *font = data;
  g_free(font->key);
  g_free(font->text);
  g_free(font->path);
}

void bosonoga_program_free(BosonogaProgram *program) {
  g_array_unref(program->ops);
  g_array_unref(program->functions);
  g_array_unref(program->rows);
  g_array_unref(program->fonts);
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
  program->ops = g_array_new(FALSE, TRUE, sizeof(BosonogaOp));
  program->functions = g_array_new(FALSE, FALSE, sizeof(BosonogaFunction));
  program->rows = g_array_new(FALSE, FALSE, sizeof(BosonogaValue));
  program->fonts = g_array_new(FALSE, FALSE, sizeof(BosonogaProgramFont));
  g_array_set_clear_func(program->fonts, bosonoga_program_font_clear);

  BosonogaParser parser;
  memset(&parser, 0, sizeof(parser));
  bosonoga_tokenizer_init(&parser.tokenizer, path, source, length);
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
