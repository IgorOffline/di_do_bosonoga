#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

// Each test/<tier>/NAME.bosonoga is generated into this function.
namespace bosonoga::program {
int info();
int shift();
}  // namespace bosonoga::program

namespace {

struct Program {
  const char *name;
  int (*bosonoga_main)();
};

struct Tier {
  const char *name;
  std::vector<Program> programs;
};

// A program's info lines are shown only when it fails.
bool passes(const char *tier, const Program &program) {
  std::ostringstream output;
  std::streambuf *const shown = std::cout.rdbuf(output.rdbuf());
  const int status = program.bosonoga_main();
  std::cout.rdbuf(shown);
  if (status == EXIT_SUCCESS) {
    return true;
  }
  std::cerr << "FAIL " << tier << '/' << program.name
            << ".bosonoga: returned exit_failure\n"
            << output.str();
  return false;
}

}  // namespace

int main() {
  const Tier tiers[] = {
      {"silver", {{"info", bosonoga::program::info}}},
      {"gold", {{"shift", bosonoga::program::shift}}},
      {"platinum", {}},
  };

  std::size_t passed = 0;
  std::size_t total = 0;
  std::ostringstream counts;
  for (const Tier &tier : tiers) {
    std::size_t tier_passed = 0;
    for (const Program &program : tier.programs) {
      tier_passed += passes(tier.name, program) ? 1 : 0;
    }
    counts << (&tier == tiers ? "" : " ")
           << tier.name << ' ' << tier_passed << '/' << tier.programs.size();
    passed += tier_passed;
    total += tier.programs.size();
  }
  std::cout << passed << '/' << total << " (" << counts.str() << ")\n\n";
  return total > 0 && passed == total ? EXIT_SUCCESS : EXIT_FAILURE;
}
