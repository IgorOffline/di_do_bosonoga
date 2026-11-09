#include <string.h>

#include "parser/internal.h"

static gboolean bosonoga_parse_gfx_sections(BosonogaParser *parser,
                                            GError **error) {
  static const struct {
    const gchar *word;
    BosonogaSection section;
  } sections[] = {
      {"gfx_global", BOSONOGA_SECTION_GFX_GLOBAL},
      {"gfx_init", BOSONOGA_SECTION_GFX_INIT},
      {"gfx_frame", BOSONOGA_SECTION_GFX_FRAME},
      {"gfx_cleanup", BOSONOGA_SECTION_GFX_CLEANUP},
      {"gfx_main", BOSONOGA_SECTION_MAIN},
  };
  const gchar *expected =
      "main, gfx_global, gfx_init, gfx_frame, gfx_cleanup or gfx_main";
  gboolean is_first = TRUE;
  do {
    gsize i = 0;
    while (i < G_N_ELEMENTS(sections) &&
           !bosonoga_at_word(parser, sections[i].word)) {
      ++i;
    }
    if (i == G_N_ELEMENTS(sections)) {
      return bosonoga_parse_error(parser, expected, error);
    }
    expected = "gfx_init, gfx_frame, gfx_cleanup or gfx_main";

    const BosonogaSection section = sections[i].section;
    if (parser->has_section[section]) {
      return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                              BOSONOGA_ERROR_SYNTAX,
                              "there is already a %s block", sections[i].word);
    }
    // Its variables must be declared before the sections that use them.
    if (section == BOSONOGA_SECTION_GFX_GLOBAL && !is_first) {
      return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                              BOSONOGA_ERROR_SYNTAX,
                              "gfx_global must be the first block");
    }
    is_first = FALSE;
    parser->has_section[section] = TRUE;
    parser->program->entries[section] = parser->ops->len;
    if (!bosonoga_advance(parser, error)) {
      return FALSE;
    }

    parser->section = section;
    if (section == BOSONOGA_SECTION_GFX_FRAME) {
      parser->delta_time_slot = parser->slot_count++;
      bosonoga_emit(parser, BOSONOGA_OP_GFX_DELTA_TIME,
                    &parser->tokenizer.token)
          ->slot = parser->delta_time_slot;
    }
    if (!bosonoga_parse_block(parser, error)) {
      return FALSE;
    }
    bosonoga_emit(parser,
                  section == BOSONOGA_SECTION_MAIN ? BOSONOGA_OP_MISSING_RETURN
                                                   : BOSONOGA_OP_SECTION_END,
                  &parser->tokenizer.token);
  } while (parser->tokenizer.token.kind != BOSONOGA_TOKEN_END);

  if (!parser->has_section[BOSONOGA_SECTION_MAIN]) {
    return bosonoga_fail_at(parser, &parser->tokenizer.token, error,
                            BOSONOGA_ERROR_SYNTAX,
                            "a gfx program needs a gfx_main block");
  }
  parser->program->is_gfx = TRUE;
  return TRUE;
}

static gboolean bosonoga_parse_program(BosonogaParser *parser, GError **error) {
  if (!bosonoga_advance(parser, error)) {
    return FALSE;
  }
  if (bosonoga_at_word(parser, "main")) {
    parser->has_section[BOSONOGA_SECTION_MAIN] = TRUE;
    if (!bosonoga_advance(parser, error) ||
        !bosonoga_parse_block(parser, error)) {
      return FALSE;
    }
    if (parser->tokenizer.token.kind != BOSONOGA_TOKEN_END) {
      return bosonoga_parse_error(parser, "end of file", error);
    }
    bosonoga_emit(parser, BOSONOGA_OP_MISSING_RETURN, &parser->tokenizer.token);
  } else if (!bosonoga_parse_gfx_sections(parser, error)) {
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

void bosonoga_program_free(BosonogaProgram *program) {
  g_array_unref(program->ops);
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
  g_autoptr(BosonogaProgram) program = g_new0(BosonogaProgram, 1);
  program->path = path;
  program->ops = g_array_new(FALSE, TRUE, sizeof(BosonogaOp));

  BosonogaParser parser;
  memset(&parser, 0, sizeof(parser));
  bosonoga_tokenizer_init(&parser.tokenizer, path, source, length);
  parser.ops = program->ops;
  parser.variables = variables;
  parser.loops = loops;
  parser.program = program;
  parser.settings = settings;
  if (!bosonoga_parse_program(&parser, error)) {
    return NULL;
  }
  program->slot_count = parser.slot_count;
  return g_steal_pointer(&program);
}
