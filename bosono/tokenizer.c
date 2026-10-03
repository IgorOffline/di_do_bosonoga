#include "tokenizer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "validator.h"

static int tokenize_into(char const program[static BOSONO_LIMIT],
                         Tokens* output) {
  size_t position = 0;
  while (position < BOSONO_LIMIT && program[position] != '\0') {
    if (isspace((unsigned char)program[position])) {
      position++;
      continue;
    }

    size_t const start = position;
    bool in_quotes = false;
    size_t string_start = 0;
    while (position < BOSONO_LIMIT && program[position] != '\0') {
      char const current = program[position];
      if (!in_quotes && isspace((unsigned char)current)) {
        break;
      }
      if (in_quotes && current == '\\' && position + 1 < BOSONO_LIMIT &&
          program[position + 1] != '\0') {
        position += 2;
        continue;
      }
      if (current == '"') {
        if (in_quotes) {
          size_t const string_length = position - string_start;
          if (string_length >= BOSONO_STRING_LIMIT) {
            fprintf(stderr, "String exceeds %d bytes\n",
                    BOSONO_STRING_LIMIT - 1);
            return EXIT_FAILURE;
          }
          if (!valid_utf8(program + string_start, string_length)) {
            fprintf(stderr, "String is not valid UTF-8\n");
            return EXIT_FAILURE;
          }
        } else {
          string_start = position + 1;
        }
        in_quotes = !in_quotes;
      }
      position++;
    }
    if (in_quotes) {
      fprintf(stderr, "Unterminated string\n");
      return EXIT_FAILURE;
    }
    if (output->count >= BOSONO_TOKEN_LIMIT) {
      fprintf(stderr, "Token capacity exceeded\n");
      return EXIT_FAILURE;
    }
    output->items[output->count] =
        (Token){.text = program + start, .length = position - start};
    output->count++;
  }
  return EXIT_SUCCESS;
}

int tokenize(char const program[static BOSONO_LIMIT], Tokens* output) {
  output->count = 0;
  int const result = tokenize_into(program, output);
  if (result != EXIT_SUCCESS) output->count = 0;
  return result;
}
