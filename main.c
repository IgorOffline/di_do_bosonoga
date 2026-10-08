#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOSONOGA_LINES 7
#define BOSONOGA_LINE_LENGTH 255
#define BOSONOGA_TOKENS_PER_LINE 8
#define BOSONOGA_TOKEN_LENGTH 63
#define BOSONOGA_TOKENS (BOSONOGA_LINES * BOSONOGA_TOKENS_PER_LINE)

#define BOSONOGA_EXIT_SUCCESS 0
#define BOSONOGA_EXIT_FAILURE 1
#define BOSONOGA_STRINGS_EQUAL 0

#define BOSONOGA_RECORD typedef struct
#define BOSONOGA_SIZE size_t
#define BOSONOGA_MALLOC(size) malloc(size)
#define BOSONOGA_FREE(ptr) free(ptr)
#define BOSONOGA_MEMCPY(dest, src, size) memcpy(dest, src, size)
#define BOSONOGA_STRLEN(str) strlen(str)
#define BOSONOGA_STRCMP(a, b) strcmp(a, b)

BOSONOGA_RECORD { char data[BOSONOGA_LINE_LENGTH + 1]; }
BosonogaLine;

BOSONOGA_RECORD { char data[BOSONOGA_TOKEN_LENGTH + 1]; }
BosonogaToken;

BOSONOGA_RECORD {
  BosonogaLine lines[BOSONOGA_LINES];
  BosonogaToken tokens[BOSONOGA_TOKENS];
  BOSONOGA_SIZE line_count;
  BOSONOGA_SIZE token_count;
}
Regina;

static int parse_input(const char* input, Regina* regina) {
  while (*input != '\0') {
    if (regina->line_count == BOSONOGA_LINES) {
      return BOSONOGA_EXIT_FAILURE;
    }

    const char* end = strchr(input, '\n');
    BOSONOGA_SIZE length =
        end == NULL ? BOSONOGA_STRLEN(input) : (BOSONOGA_SIZE)(end - input);

    if (length > BOSONOGA_LINE_LENGTH) {
      return BOSONOGA_EXIT_FAILURE;
    }

    BosonogaLine* line = &regina->lines[regina->line_count++];

    BOSONOGA_MEMCPY(line->data, input, length);
    line->data[length] = '\0';

    input += length;

    if (*input == '\n') {
      input++;
    }
  }

  return BOSONOGA_EXIT_SUCCESS;
}

static int parse_lines(Regina* regina) {
  for (BOSONOGA_SIZE i = 0; i < regina->line_count; i++) {
    const char* current = regina->lines[i].data;
    BOSONOGA_SIZE line_tokens = 0;

    while (*current != '\0') {
      while (*current == ' ') {
        current++;
      }

      if (*current == '\0') {
        break;
      }

      const char* start = current;

      while (*current != '\0' && *current != ' ') {
        current++;
      }

      BOSONOGA_SIZE length = (BOSONOGA_SIZE)(current - start);

      if (length > BOSONOGA_TOKEN_LENGTH ||
          line_tokens == BOSONOGA_TOKENS_PER_LINE) {
        return BOSONOGA_EXIT_FAILURE;
      }

      BosonogaToken* token = &regina->tokens[regina->token_count++];

      BOSONOGA_MEMCPY(token->data, start, length);
      token->data[length] = '\0';

      line_tokens++;
    }
  }

  return BOSONOGA_EXIT_SUCCESS;
}

static void print_tokens(const Regina* regina) {
  for (BOSONOGA_SIZE i = 0; i < regina->token_count; i++) {
    if (BOSONOGA_STRCMP(regina->tokens[i].data, "version") ==
        BOSONOGA_STRINGS_EQUAL) {
      printf("[V]\n");
      i++;
    } else if (BOSONOGA_STRCMP(regina->tokens[i].data, "lega") ==
               BOSONOGA_STRINGS_EQUAL) {
      printf("[L]\n");
      i++;
    } else if (BOSONOGA_STRCMP(regina->tokens[i].data, "aka") ==
               BOSONOGA_STRINGS_EQUAL) {
      printf("[A]\n");
      i += 2;
    } else if (BOSONOGA_STRCMP(regina->tokens[i].data, "main") ==
               BOSONOGA_STRINGS_EQUAL) {
      printf("[M]\n");
    } else {
      printf("[%s]\n", regina->tokens[i].data);
    }
  }
}

int main(void) {
  const char* const input =
      "version 0.2.0\nlega _bosonoga_if\nlega _bosonoga_gt\naka _bosonoga_if "
      "if\naka "
      "_bosonoga_gt gt\nmain\nif 1 gt 0\n";

  Regina* regina = BOSONOGA_MALLOC(sizeof(Regina));

  if (regina == NULL) {
    return BOSONOGA_EXIT_FAILURE;
  }

  *regina = (Regina){0};

  int status = parse_input(input, regina);

  printf("Regina allocated: %zu bytes\n", sizeof(Regina));

  if (status == BOSONOGA_EXIT_SUCCESS) {
    status = parse_lines(regina);
  }

  if (status == BOSONOGA_EXIT_SUCCESS) {
    print_tokens(regina);
  }

  BOSONOGA_FREE(regina);
  return status;
}
