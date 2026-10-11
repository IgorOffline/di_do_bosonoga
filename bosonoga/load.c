#include <stdarg.h>
#include <string.h>

#include "bosonoga.h"

#define BOSONOGA_MAIN_LIMIT 1
#define BOSONOGA_COMMENT_PREFIX "//"

// clang-format off
G_DEFINE_QUARK(bosonoga-error-quark, bosonoga_error)
// clang-format on

static gboolean is_blank(gchar character) {
  return character == ' ' || character == '\t';
}

static gboolean starts_comment(const gchar* text) {
  return g_str_has_prefix(text, BOSONOGA_COMMENT_PREFIX);
}

gboolean bosonoga_read_source(const gchar* path,
                              gchar destination[BOSONOGA_SOURCE_LENGTH + 1],
                              GError** error) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(destination != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  g_autofree gchar* contents = NULL;
  gsize length = 0;
  if (!g_file_get_contents(path, &contents, &length, error)) {
    return FALSE;
  }
  if (length > BOSONOGA_SOURCE_LENGTH) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_SOURCE,
                "%s: %" G_GSIZE_FORMAT " bytes, the limit is %" G_GSIZE_FORMAT,
                path, length, (gsize)BOSONOGA_SOURCE_LENGTH);
    return FALSE;
  }
  if (memchr(contents, '\0', length) != NULL) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_SOURCE,
                "%s: contains a NUL byte", path);
    return FALSE;
  }
  if (!g_utf8_validate_len(contents, length, NULL)) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_SOURCE,
                "%s: is not valid UTF-8", path);
    return FALSE;
  }
  memcpy(destination, contents, length + 1);
  return TRUE;
}

static gsize count_line_tokens(const gchar* line) {
  gsize count = 0;
  while (TRUE) {
    while (is_blank(*line)) {
      line++;
    }
    if (*line == '\0' || starts_comment(line)) {
      return count;
    }
    while (*line != '\0' && !is_blank(*line)) {
      line++;
    }
    count++;
  }
}

static gchar* describe_line(const Regina* regina, gsize line_index) {
  if (line_index < regina->header_line_count) {
    return g_strdup_printf("header line %" G_GSIZE_FORMAT, line_index + 1);
  }
  return g_strdup_printf("line %" G_GSIZE_FORMAT,
                         line_index - regina->header_line_count + 1);
}

static gchar* describe_position(const Regina* regina, gsize position) {
  gsize seen = 0;
  for (gsize i = 0; i < regina->line_count; i++) {
    seen += count_line_tokens(regina->lines[i].data);
    if (position < seen) {
      return describe_line(regina, i);
    }
  }
  return g_strdup("end of program");
}

gboolean bosonoga_fail_at(const Regina* regina, gsize position, GError** error,
                          BosonogaError code, const gchar* format, ...) {
  va_list args;
  va_start(args, format);
  g_autofree gchar* message = g_strdup_vprintf(format, args);
  va_end(args);
  g_autofree gchar* where = describe_position(regina, position);
  g_set_error(error, BOSONOGA_ERROR, (gint)code, "%s: %s", where, message);
  return FALSE;
}

static gboolean split_into_lines(Regina* regina, const gchar* text,
                                 const gchar* origin, GError** error) {
  gsize line_number = 0;
  while (*text != '\0') {
    line_number++;
    if (regina->line_count == BOSONOGA_LINE_LIMIT) {
      g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_LIMIT,
                  "%s line %" G_GSIZE_FORMAT ": more than %" G_GSIZE_FORMAT
                  " lines in total",
                  origin, line_number, (gsize)BOSONOGA_LINE_LIMIT);
      return FALSE;
    }

    const gchar* newline = strchr(text, '\n');
    const gsize raw_length =
        newline == NULL ? strlen(text) : (gsize)(newline - text);
    gsize length = raw_length;
    if (length > 0 && text[length - 1] == '\r') {
      length--;
    }
    if (length > BOSONOGA_LINE_LENGTH) {
      g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_LIMIT,
                  "%s line %" G_GSIZE_FORMAT ": %" G_GSIZE_FORMAT
                  " characters, the limit is %d",
                  origin, line_number, length, BOSONOGA_LINE_LENGTH);
      return FALSE;
    }

    BosonogaLine* line = &regina->lines[regina->line_count++];
    memcpy(line->data, text, length);
    line->data[length] = '\0';

    text += raw_length;
    if (*text == '\n') {
      text++;
    }
  }
  return TRUE;
}

static gboolean split_line_into_tokens(Regina* regina, gsize line_index,
                                       GError** error) {
  const gchar* line = regina->lines[line_index].data;
  gsize line_token_count = 0;
  while (TRUE) {
    while (is_blank(*line)) {
      line++;
    }
    if (*line == '\0' || starts_comment(line)) {
      return TRUE;
    }

    const gchar* start = line;
    while (*line != '\0' && !is_blank(*line)) {
      line++;
    }

    const gsize length = (gsize)(line - start);
    const gchar* problem = NULL;
    if (length > BOSONOGA_TOKEN_LENGTH) {
      problem = "a word is longer than 63 characters";
    } else if (line_token_count == BOSONOGA_TOKEN_PER_LINE_LIMIT) {
      problem = "more than 128 words on one line";
    } else if (regina->token_count == BOSONOGA_TOKEN_LIMIT) {
      problem = "too many words in total";
    }
    if (problem != NULL) {
      g_autofree gchar* where = describe_line(regina, line_index);
      g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_LIMIT, "%s: %s", where,
                  problem);
      return FALSE;
    }

    BosonogaToken* token = &regina->tokens[regina->token_count++];
    memcpy(token->data, start, length);
    token->data[length] = '\0';
    line_token_count++;
  }
}

static gboolean parse_header(Regina* regina, GError** error) {
  for (gsize position = 0; position < regina->token_count; position++) {
    const gchar* keyword = token_at(regina, position);

    if (g_str_equal(keyword, "main")) {
      if (regina->main_count == BOSONOGA_MAIN_LIMIT) {
        return bosonoga_fail_at(regina, position, error, BOSONOGA_ERROR_SYNTAX,
                                "'main' appears more than once");
      }
      regina->main_count++;
    } else if (g_str_equal(keyword, "version")) {
      if (regina->version_count == BOSONOGA_VERSION_LIMIT ||
          tokens_left(regina, position) < BOSONOGA_VERSION_TOKEN_COUNT) {
        return bosonoga_fail_at(regina, position, error, BOSONOGA_ERROR_SYNTAX,
                                "'version' needs one value, given once");
      }
      BosonogaVersion* version = &regina->version[regina->version_count++];
      version->len = g_strlcpy(version->string, token_at(regina, position + 1),
                               sizeof version->string);
      position += BOSONOGA_VERSION_TOKEN_COUNT - 1;
    } else if (g_str_equal(keyword, "lega")) {
      if (regina->lega_count == BOSONOGA_LEGA_LIMIT ||
          tokens_left(regina, position) < BOSONOGA_LEGA_TOKEN_COUNT) {
        return bosonoga_fail_at(regina, position, error, BOSONOGA_ERROR_LIMIT,
                                "'lega' needs a name (at most %d of them)",
                                BOSONOGA_LEGA_LIMIT);
      }
      BosonogaLega* lega = &regina->lega[regina->lega_count++];
      lega->len = g_strlcpy(lega->legal, token_at(regina, position + 1),
                            sizeof lega->legal);
      position += BOSONOGA_LEGA_TOKEN_COUNT - 1;
    } else if (g_str_equal(keyword, "aka")) {
      if (regina->aka_count == BOSONOGA_AKA_LIMIT ||
          tokens_left(regina, position) < BOSONOGA_AKA_TOKEN_COUNT) {
        return bosonoga_fail_at(regina, position, error, BOSONOGA_ERROR_LIMIT,
                                "'aka' needs two names (at most %d of them)",
                                BOSONOGA_AKA_LIMIT);
      }
      BosonogaAka* aka = &regina->aka[regina->aka_count++];
      aka->original_len = g_strlcpy(
          aka->original, token_at(regina, position + 1), sizeof aka->original);
      aka->replacement_len =
          g_strlcpy(aka->replacement, token_at(regina, position + 2),
                    sizeof aka->replacement);
      position += BOSONOGA_AKA_TOKEN_COUNT - 1;
    }
  }
  return TRUE;
}

gboolean bosonoga_load_program(Regina* regina, const gchar* header,
                               const gchar* input, GError** error) {
  g_return_val_if_fail(regina != NULL, FALSE);
  g_return_val_if_fail(header != NULL, FALSE);
  g_return_val_if_fail(input != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  if (!split_into_lines(regina, header, "header", error)) {
    return FALSE;
  }
  regina->header_line_count = regina->line_count;
  if (!split_into_lines(regina, input, "program", error)) {
    return FALSE;
  }
  for (gsize i = 0; i < regina->line_count; i++) {
    if (!split_line_into_tokens(regina, i, error)) {
      return FALSE;
    }
  }
  return parse_header(regina, error);
}
