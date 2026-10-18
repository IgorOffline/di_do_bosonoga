#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CORELA_PARSE_EXIT_SUCCESS 0
#define CORELA_PARSE_EXIT_FAILURE 1

typedef struct {
  const char *before;
  const char *after;
} CorelaReplacement;

int main(void) {
  FILE *input = fopen("corela/main.corela", "r");
  if (input == NULL) {
    printf("corela/main.corela?\n");
    return CORELA_PARSE_EXIT_FAILURE;
  }

  FILE *output = fopen("main.c", "w");
  if (output == NULL) {
    printf("main.c?\n");
    fclose(input);
    return CORELA_PARSE_EXIT_FAILURE;
  }

  const CorelaReplacement replacements[] = {
      {"i32", "CORELA_I32"},         {"u32", "CORELA_U32"},
      {"and", "CORELA_AND"},         {"exit_success", "CORELA_EXIT_SUCCESS"},
      {"shift_is", "CORELA_IS"},     {"shift_0", "CORELA_SHIFT_0"},
      {"shift_1", "CORELA_SHIFT_1"}, {"or", "CORELA_SHIFT_OR"},
  };
  const size_t replacement_count =
      sizeof(replacements) / sizeof(replacements[0]);

  char line[4096];
  while (fgets(line, sizeof(line), input) != NULL) {
    // Ignore surrounding whitespace when checking for a bracket line.
    const char *start = line;
    while (isspace((unsigned char)*start)) {
      ++start;
    }

    // An include directive starts at the first non-whitespace character.
    if (strncmp(start, "include", 7) == 0 &&
        (isspace((unsigned char)start[7]) || start[7] == '<' ||
         start[7] == '"')) {
      fwrite(line, 1, (size_t)(start - line), output);
      fputc('#', output);
      fputs(start, output);
      continue;
    }

    const char *trimmed_end = start + strlen(start);
    while (trimmed_end > start && isspace((unsigned char)trimmed_end[-1])) {
      --trimmed_end;
    }

    if (trimmed_end - start == 4 && strncmp(start, "main", 4) == 0) {
      fwrite(line, 1, (size_t)(start - line), output);
      fputs("int main(void)", output);
      fputs(trimmed_end, output);
      continue;
    }

    if (trimmed_end - start == 2 &&
        (strncmp(start, "di", 2) == 0 || strncmp(start, "do", 2) == 0)) {
      fwrite(line, 1, (size_t)(start - line), output);
      fputc(start[1] == 'i' ? '{' : '}', output);
      fputs(trimmed_end, output);
      continue;
    }

    const char *position = line;
    while (*position != '\0') {
      const char *nearest = NULL;
      const CorelaReplacement *selected = NULL;

      for (size_t i = 0; i < replacement_count; ++i) {
        const char *found = strstr(position, replacements[i].before);
        while (found != NULL) {
          const char *end = found + strlen(replacements[i].before);
          if ((found == line ||
               (!isalnum((unsigned char)found[-1]) && found[-1] != '_')) &&
              !isalnum((unsigned char)*end) && *end != '_') {
            break;
          }
          found = strstr(found + 1, replacements[i].before);
        }
        if (found != NULL && (nearest == NULL || found < nearest)) {
          nearest = found;
          selected = &replacements[i];
        }
      }

      if (selected == NULL) {
        fputs(position, output);
        break;
      }

      fwrite(position, 1, (size_t)(nearest - position), output);
      fputs(selected->after, output);
      position = nearest + strlen(selected->before);
    }
  }

  fclose(input);
  fclose(output);
  printf("+main.c\n");
  return CORELA_PARSE_EXIT_SUCCESS;
}
