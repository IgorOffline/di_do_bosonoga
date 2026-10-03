#include <stdio.h>
#include <stdlib.h>

#include "core/core.h"

int main(void) {
  Regina* const regina = malloc(REGINA_BYTES);
  if (!regina) {
    fputs("Unable to allocate Regina\n", stderr);
    return EXIT_FAILURE;
  }
  int const status = core(regina);
  free(regina);
  return status == EXIT_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
