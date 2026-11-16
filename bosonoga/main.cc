// Runs each test/<tier>/NAME.bosonoga, from the directory that holds test, as
// the C bosonoga test runner did: a program passes when it returns
// exit_success, and, when there is a NAME.xp.bosonoga, prints what it expects.
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "programs.hh"
#include "test/expected_output.hh"

namespace {

struct Program {
  const char *name;
  int (*bosonoga_main)();
};

struct Tier {
  const char *name;
  const char *directory;
  std::vector<Program> programs;
};

// What a program prints is shown only when it fails.
bool passes(const Tier &tier, const Program &program) {
  const std::string stem = std::string(tier.directory) + '/' + program.name;
  std::ostringstream output;
  std::streambuf *const shown = std::cout.rdbuf(output.rdbuf());
  const int status = program.bosonoga_main();
  std::cout.rdbuf(shown);
  if (status != EXIT_SUCCESS) {
    std::cerr << "FAIL " << stem << ".bosonoga: returned exit_failure\n"
              << output.str();
    return false;
  }
  const auto expected = bosonoga::tests::read_expected(stem + ".xp.bosonoga");
  if (!expected.problem.empty()) {
    std::cerr << "FAIL " << stem << ".bosonoga: " << expected.problem << '\n';
    return false;
  }
  if (expected.exists && expected.printed != output.str()) {
    std::cerr << "FAIL " << stem << ".bosonoga: printed '" << output.str()
              << "', but expected '" << expected.printed << "'\n";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  const std::vector<Tier> tiers = BOSONOGA_TEST_TIERS;

  std::size_t passed = 0;
  std::size_t total = 0;
  std::ostringstream counts;
  for (const Tier &tier : tiers) {
    std::size_t tier_passed = 0;
    for (const Program &each : tier.programs) {
      tier_passed += passes(tier, each) ? 1 : 0;
    }
    counts << (&tier == &tiers.front() ? "" : " ") << tier.name << ' ' << tier_passed
           << '/' << tier.programs.size();
    passed += tier_passed;
    total += tier.programs.size();
  }
  std::cout << passed << '/' << total << " (" << counts.str() << ")\n\n";
  return total > 0 && passed == total ? EXIT_SUCCESS : EXIT_FAILURE;
}
