#include "tokenizer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "validator.h"

static int tokenize_into(char const program[static BOSONO_LIMIT],
                         Tokens* output) {
  size_t position = 0;
  while (position < BOSONO_LIMIT && program[position] != '\0') {
    if (isspace((unsigned char)program[position])) {
      position++;
      continue;
    }
    if (program[position] == '/' && position + 1 < BOSONO_LIMIT &&
        program[position + 1] == '/') {
      while (position < BOSONO_LIMIT && program[position] != '\0' &&
             program[position] != '\n' && program[position] != '\r') {
        position++;
      }
      continue;
    }

    size_t const start = position;
    bool in_quotes = false;
    size_t string_start = 0;
    while (position < BOSONO_LIMIT && program[position] != '\0') {
      char const current = program[position];
      if (!in_quotes && (isspace((unsigned char)current) ||
                         (current == '/' && position + 1 < BOSONO_LIMIT &&
                          program[position + 1] == '/'))) {
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

static bool token_matches(Token const* token, char const* name) {
  size_t const length = strlen(name);
  return token->length == length && memcmp(token->text, name, length) == 0;
}

static int declaration_kind(Token const* token, bool legal,
                            BosonoAliasKind* kind) {
  if (token_matches(token, legal ? "lega" : "aka"))
    *kind = BOSONO_ALIAS_EXACT;
  else if (token_matches(token, legal ? "lega_prefix" : "aka_prefix"))
    *kind = BOSONO_ALIAS_PREFIX;
  else if (token_matches(token, legal ? "lega_suffix" : "aka_suffix"))
    *kind = BOSONO_ALIAS_SUFFIX;
  else
    return EXIT_FAILURE;
  return EXIT_SUCCESS;
}

int load_aliases(Tokens const* tokens, BosonoAliases* aliases) {
  aliases->count = 0;
  if (tokens->count % 3 != 0 || tokens->count / 3 > BOSONO_ALIAS_LIMIT)
    return EXIT_FAILURE;
  for (size_t i = 0; i < tokens->count; i += 3) {
    BosonoAlias* alias = &aliases->items[aliases->count];
    if (declaration_kind(&tokens->items[i], false, &alias->kind) !=
        EXIT_SUCCESS)
      return EXIT_FAILURE;
    char* names[2] = {alias->original, alias->shorter};
    for (size_t n = 0; n < 2; n++) {
      Token const* token = &tokens->items[i + n + 1];
      if (!token->length || token->length >= BOSONO_ALIAS_NAME_LIMIT)
        return EXIT_FAILURE;
      for (size_t c = 0; c < token->length; c++)
        if (!(isalnum((unsigned char)token->text[c]) || token->text[c] == '_'))
          return EXIT_FAILURE;
      memcpy(names[n], token->text, token->length);
      names[n][token->length] = '\0';
    }
    for (size_t j = 0; j < aliases->count; j++)
      if (strcmp(aliases->items[j].shorter, alias->shorter) == 0)
        return EXIT_FAILURE;
    aliases->count++;
  }
  return EXIT_SUCCESS;
}

int apply_aliases(char program[static BOSONO_LIMIT], Tokens* tokens,
                  BosonoAliases const* aliases) {
  size_t used = strlen(program) + 1;
  for (size_t i = 0; i < tokens->count; i++) {
    Token* token = &tokens->items[i];
    if (token->text[0] == '"') continue;
    for (size_t j = 0; j < aliases->count; j++) {
      BosonoAlias const* alias = &aliases->items[j];
      size_t const length = strlen(alias->shorter);
      size_t const original_length = strlen(alias->original);
      if (alias->kind == BOSONO_ALIAS_EXACT &&
          token_matches(token, alias->shorter)) {
        token->text = alias->original;
        token->length = original_length;
        break;
      }
      // The declaration names describe the value attached to the alias.
      bool const suffix = alias->kind == BOSONO_ALIAS_SUFFIX &&
                          token->length > length + 1 &&
                          token->text[length] == '_' &&
                          memcmp(token->text, alias->shorter, length) == 0;
      bool const prefix =
          alias->kind == BOSONO_ALIAS_PREFIX && token->length > length + 1 &&
          token->text[token->length - length - 1] == '_' &&
          !(token->length >= original_length &&
            memcmp(token->text + token->length - original_length,
                   alias->original, original_length) == 0) &&
          memcmp(token->text + token->length - length, alias->shorter,
                 length) == 0;
      if (!prefix && !suffix) continue;
      size_t const remainder = token->length - length - (prefix ? 1U : 0U);
      size_t const expanded = original_length + remainder;
      if (expanded + 1 > BOSONO_LIMIT - used) return EXIT_FAILURE;
      char* destination = program + used;
      if (suffix) {
        memcpy(destination, alias->original, original_length);
        memcpy(destination + original_length, token->text + length, remainder);
      } else {
        memcpy(destination, token->text, remainder);
        memcpy(destination + remainder, alias->original, original_length);
      }
      destination[expanded] = '\0';
      token->text = destination;
      token->length = expanded;
      used += expanded + 1;
      break;
    }
  }
  return EXIT_SUCCESS;
}

int validate_aliases(Tokens const* legal, BosonoAliases const* aliases) {
  if (legal->count % 2 != 0) return EXIT_FAILURE;
  for (size_t i = 0; i < legal->count; i += 2) {
    BosonoAliasKind kind;
    if (declaration_kind(&legal->items[i], true, &kind) != EXIT_SUCCESS)
      return EXIT_FAILURE;
    Token const* name = &legal->items[i + 1];
    if (!name->length || name->length >= BOSONO_ALIAS_NAME_LIMIT)
      return EXIT_FAILURE;
    for (size_t c = 0; c < name->length; c++)
      if (!(isalnum((unsigned char)name->text[c]) || name->text[c] == '_'))
        return EXIT_FAILURE;
  }
  for (size_t i = 0; i < aliases->count; i++) {
    BosonoAlias const* alias = &aliases->items[i];
    bool found = false;
    for (size_t j = 0; j < legal->count; j += 2) {
      BosonoAliasKind kind;
      if (declaration_kind(&legal->items[j], true, &kind) == EXIT_SUCCESS &&
          kind == alias->kind &&
          token_matches(&legal->items[j + 1], alias->original))
        found = true;
    }
    if (!found) {
      fprintf(stderr,
              "Alias original %s with its attachment kind is not enabled in "
              "lega.bosonoga\n",
              alias->original);
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}
