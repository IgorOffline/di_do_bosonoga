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
  if (!bosonoga_parse_block(parser, error)) {
    return FALSE;
  }
  bosonoga_emit(parser,
                section == BOSONOGA_SECTION_MAIN ? BOSONOGA_OP_MISSING_RETURN
                                                 : BOSONOGA_OP_SECTION_END,
                &parser->tokenizer.token);
  return TRUE;
}

// What lists the items that can be here, for the error if this is none.
static gboolean bosonoga_parse_gfx_section(BosonogaParser *parser,
                                           const gchar *what, GError **error) {
  static const struct {
    const gchar *word;
    BosonogaSection section;
  } sections[] = {
      {"gfx_init", BOSONOGA_SECTION_GFX_INIT},
      {"gfx_frame", BOSONOGA_SECTION_GFX_FRAME},
      {"gfx_cleanup", BOSONOGA_SECTION_GFX_CLEANUP},
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
    if (bosonoga_at_word(parser, "def")) {
      if (!bosonoga_parse_function(parser, error)) {
        return FALSE;
      }
    } else if (bosonoga_at_word(parser, "array")) {
      if (!bosonoga_parse_array(parser, error)) {
        return FALSE;
      }
    } else if (bosonoga_at_word(parser, "bosonoga_global")) {
      // Its variables must be declared before the sections that use them.
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
      return bosonoga_parse_error(parser, "def, array or end of file", error);
    } else {
      const gchar *what =
          has_gfx_section
              ? "def, array, gfx_init, gfx_frame, gfx_cleanup or gfx_main"
          : parser->has_section[BOSONOGA_SECTION_GLOBAL]
              ? "def, array, bosonoga_main, gfx_init, gfx_frame, gfx_cleanup "
                "or gfx_main"
              : "def, array, bosonoga_global, bosonoga_main, gfx_init, "
                "gfx_frame, gfx_cleanup or gfx_main";
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

void bosonoga_program_free(BosonogaProgram *program) {
  g_array_unref(program->ops);
  g_array_unref(program->functions);
  g_array_unref(program->rows);
  g_free(program);
}

BosonogaProgram *bosonoga_parse(const gchar *path, const gchar *source,
                                gsize length, const BosonogaSettings *settings,
                                GError **error) {
  g_autoptr(GArray) variables =
      g_array_new(FALSE, FALSE, sizeof(BosonogaVariable));
  // A loop that a failed parse leaves open is freed with the array.
  g_autoptr(GArray) loops = g_array_new(FALSE, FALSE, sizeof(BosonogaLoop));
  g_array_set_clear_func(loops, bosonoga_loop_clear);
  g_autoptr(GArray) call_blocks =
      g_array_new(FALSE, FALSE, sizeof(BosonogaCallBlock));
  g_autoptr(BosonogaProgram) program = g_new0(BosonogaProgram, 1);
  program->path = path;
  program->ops = g_array_new(FALSE, TRUE, sizeof(BosonogaOp));
  program->functions = g_array_new(FALSE, FALSE, sizeof(BosonogaFunction));
  program->rows = g_array_new(FALSE, FALSE, sizeof(BosonogaValue));

  BosonogaParser parser;
  memset(&parser, 0, sizeof(parser));
  bosonoga_tokenizer_init(&parser.tokenizer, path, source, length);
  parser.ops = program->ops;
  parser.variables = variables;
  parser.loops = loops;
  parser.functions = program->functions;
  parser.call_blocks = call_blocks;
  parser.program = program;
  parser.settings = settings;
  if (!bosonoga_parse_program(&parser, error)) {
    return NULL;
  }
  program->slot_count = parser.slot_count;
  return g_steal_pointer(&program);
}
