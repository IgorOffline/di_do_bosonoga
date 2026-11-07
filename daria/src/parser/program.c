#include <string.h>

#include "parser/internal.h"

// Parses a program that does not start with main, so it must be a gfx program.
static gboolean daria_parse_gfx_sections(DariaParser *parser, GError **error) {
  static const struct {
    const gchar *word;
    DariaSection section;
  } sections[] = {
      {"gfx_init", DARIA_SECTION_GFX_INIT},
      {"gfx_frame", DARIA_SECTION_GFX_FRAME},
      {"gfx_cleanup", DARIA_SECTION_GFX_CLEANUP},
      {"gfx_main", DARIA_SECTION_MAIN},
  };
  const gchar *expected = "main, gfx_init, gfx_frame, gfx_cleanup or gfx_main";
  do {
    gsize i = 0;
    while (i < G_N_ELEMENTS(sections) &&
           !daria_at_word(parser, sections[i].word)) {
      ++i;
    }
    if (i == G_N_ELEMENTS(sections)) {
      return daria_parse_error(parser, expected, error);
    }
    expected = "gfx_init, gfx_frame, gfx_cleanup or gfx_main";

    const DariaSection section = sections[i].section;
    if (parser->has_section[section]) {
      return daria_fail_at(parser, &parser->tokenizer.token, error,
                           DARIA_ERROR_SYNTAX, "there is already a %s block",
                           sections[i].word);
    }
    parser->has_section[section] = TRUE;
    parser->program->entries[section] = parser->ops->len;
    if (!daria_advance(parser, error)) {
      return FALSE;
    }

    parser->section = section;
    if (!daria_parse_block(parser, error)) {
      return FALSE;
    }
    daria_emit(parser,
               section == DARIA_SECTION_MAIN ? DARIA_OP_MISSING_RETURN
                                             : DARIA_OP_SECTION_END,
               &parser->tokenizer.token);
  } while (parser->tokenizer.token.kind != DARIA_TOKEN_END);

  if (!parser->has_section[DARIA_SECTION_MAIN]) {
    return daria_fail_at(parser, &parser->tokenizer.token, error,
                         DARIA_ERROR_SYNTAX,
                         "a gfx program needs a gfx_main block");
  }
  parser->program->is_gfx = TRUE;
  return TRUE;
}

static gboolean daria_parse_program(DariaParser *parser, GError **error) {
  if (!daria_advance(parser, error)) {
    return FALSE;
  }
  if (daria_at_word(parser, "main")) {
    parser->has_section[DARIA_SECTION_MAIN] = TRUE;
    if (!daria_advance(parser, error) || !daria_parse_block(parser, error)) {
      return FALSE;
    }
    if (parser->tokenizer.token.kind != DARIA_TOKEN_END) {
      return daria_parse_error(parser, "end of file", error);
    }
    daria_emit(parser, DARIA_OP_MISSING_RETURN, &parser->tokenizer.token);
  } else if (!daria_parse_gfx_sections(parser, error)) {
    return FALSE;
  }

  for (guint section = 0; section < DARIA_SECTION_COUNT; ++section) {
    if (!parser->has_section[section]) {
      parser->program->entries[section] = parser->ops->len;
      daria_emit(parser, DARIA_OP_SECTION_END, &parser->tokenizer.token);
    }
  }
  return TRUE;
}

void daria_program_free(DariaProgram *program) {
  g_array_unref(program->ops);
  g_free(program);
}

DariaProgram *daria_parse(const gchar *path, const gchar *source, gsize length,
                          const DariaSettings *settings, GError **error) {
  g_autoptr(GArray) variables =
      g_array_new(FALSE, FALSE, sizeof(DariaVariable));
  g_autoptr(DariaProgram) program = g_new0(DariaProgram, 1);
  program->path = path;
  program->ops = g_array_new(FALSE, TRUE, sizeof(DariaOp));

  DariaParser parser;
  memset(&parser, 0, sizeof(parser));
  daria_tokenizer_init(&parser.tokenizer, path, source, length);
  parser.ops = program->ops;
  parser.variables = variables;
  parser.program = program;
  parser.settings = settings;
  if (!daria_parse_program(&parser, error)) {
    return NULL;
  }
  program->slot_count = parser.slot_count;
  return g_steal_pointer(&program);
}
