#include "bosonoga.h"

#define BOSONOGA_MAIN_LIMIT 1
#define BOSONOGA_COMMENT_PREFIX "//"

static bool is_blank(char character) {
  return character == ' ' || character == '\t';
}

static bool starts_comment(const char* text) {
  return BOSONOGA_STRNCMP(text, BOSONOGA_COMMENT_PREFIX,
                          BOSONOGA_STRLEN(BOSONOGA_COMMENT_PREFIX)) ==
         BOSONOGA_STRINGS_EQUAL;
}

static int split_into_lines(const char* text, Regina* regina) {
  while (*text != '\0') {
    if (regina->line_count == BOSONOGA_LINE_LIMIT) {
      return BOSONOGA_EXIT_FAILURE;
    }

    const char* newline = BOSONOGA_STRCHR(text, '\n');
    const BOSONOGA_SIZE raw_length = newline == NULL
                                         ? BOSONOGA_STRLEN(text)
                                         : (BOSONOGA_SIZE)(newline - text);
    BOSONOGA_SIZE length = raw_length;
    if (length > 0 && text[length - 1] == '\r') {
      length--;
    }
    if (length > BOSONOGA_LINE_LENGTH) {
      return BOSONOGA_EXIT_FAILURE;
    }

    BosonogaLine* line = &regina->lines[regina->line_count++];
    BOSONOGA_MEMCPY(line->data, text, length);
    line->data[length] = '\0';

    text += raw_length;
    if (*text == '\n') {
      text++;
    }
  }
  return BOSONOGA_EXIT_SUCCESS;
}

static int split_line_into_tokens(const char* line, Regina* regina) {
  BOSONOGA_SIZE line_token_count = 0;
  while (true) {
    while (is_blank(*line)) {
      line++;
    }
    if (*line == '\0' || starts_comment(line)) {
      return BOSONOGA_EXIT_SUCCESS;
    }

    const char* start = line;
    while (*line != '\0' && !is_blank(*line)) {
      line++;
    }

    const BOSONOGA_SIZE length = (BOSONOGA_SIZE)(line - start);
    if (length > BOSONOGA_TOKEN_LENGTH ||
        line_token_count == BOSONOGA_TOKEN_PER_LINE_LIMIT ||
        regina->token_count == BOSONOGA_TOKEN_LIMIT) {
      return BOSONOGA_EXIT_FAILURE;
    }

    BosonogaToken* token = &regina->tokens[regina->token_count++];
    BOSONOGA_MEMCPY(token->data, start, length);
    token->data[length] = '\0';
    line_token_count++;
  }
}

static int split_lines_into_tokens(Regina* regina) {
  for (BOSONOGA_SIZE i = 0; i < regina->line_count; i++) {
    if (split_line_into_tokens(regina->lines[i].data, regina) !=
        BOSONOGA_EXIT_SUCCESS) {
      return BOSONOGA_EXIT_FAILURE;
    }
  }
  return BOSONOGA_EXIT_SUCCESS;
}

static int parse_header(Regina* regina) {
  for (BOSONOGA_SIZE position = 0; position < regina->token_count; position++) {
    const char* keyword = token_at(regina, position);

    if (strings_equal(keyword, "main")) {
      if (regina->main_count == BOSONOGA_MAIN_LIMIT) {
        return BOSONOGA_EXIT_FAILURE;
      }
      regina->main_count++;
    } else if (strings_equal(keyword, "version")) {
      if (regina->version_count == BOSONOGA_VERSION_LIMIT ||
          tokens_left(regina, position) < BOSONOGA_VERSION_TOKEN_COUNT) {
        return BOSONOGA_EXIT_FAILURE;
      }
      BosonogaVersion* version = &regina->version[regina->version_count++];
      version->len = copy_text(version->string, token_at(regina, position + 1));
      position += BOSONOGA_VERSION_TOKEN_COUNT - 1;
    } else if (strings_equal(keyword, "lega")) {
      if (regina->lega_count == BOSONOGA_LEGA_LIMIT ||
          tokens_left(regina, position) < BOSONOGA_LEGA_TOKEN_COUNT) {
        return BOSONOGA_EXIT_FAILURE;
      }
      BosonogaLega* lega = &regina->lega[regina->lega_count++];
      lega->len = copy_text(lega->legal, token_at(regina, position + 1));
      position += BOSONOGA_LEGA_TOKEN_COUNT - 1;
    } else if (strings_equal(keyword, "aka")) {
      if (regina->aka_count == BOSONOGA_AKA_LIMIT ||
          tokens_left(regina, position) < BOSONOGA_AKA_TOKEN_COUNT) {
        return BOSONOGA_EXIT_FAILURE;
      }
      BosonogaAka* aka = &regina->aka[regina->aka_count++];
      aka->original_len =
          copy_text(aka->original, token_at(regina, position + 1));
      aka->replacement_len =
          copy_text(aka->replacement, token_at(regina, position + 2));
      position += BOSONOGA_AKA_TOKEN_COUNT - 1;
    }
  }
  return BOSONOGA_EXIT_SUCCESS;
}

int bosonoga_load_program(Regina* regina, const char* header,
                          const char* input) {
  if (split_into_lines(header, regina) != BOSONOGA_EXIT_SUCCESS ||
      split_into_lines(input, regina) != BOSONOGA_EXIT_SUCCESS ||
      split_lines_into_tokens(regina) != BOSONOGA_EXIT_SUCCESS ||
      parse_header(regina) != BOSONOGA_EXIT_SUCCESS) {
    return BOSONOGA_EXIT_FAILURE;
  }
  return BOSONOGA_EXIT_SUCCESS;
}
