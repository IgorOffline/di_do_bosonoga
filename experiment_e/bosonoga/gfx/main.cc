// Runs the gfx program that it is linked with in a window, as the C bosonoga's
// gfx runner does, and prints why it fails.
#include <cstdlib>
#include <iostream>

#include "bosonoga_core.hh"

namespace bosonoga {
int gfx_program();
}

int main() {
  try {
    return bosonoga::gfx_program();
  } catch (const bosonoga::runtime::Error &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
