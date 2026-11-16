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

// Each test/<tier>/NAME.bosonoga is generated into this function.
namespace bosonoga::program {
int accumulate();
int arithmetic();
int array();
int boolean();
int branch();
int compare();
int convert();
int function();
int info();
int join();
int loop();
int print();
int recursion();
int results();
int shift();
int text();
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

struct Expected {
  bool exists = false;
  std::string printed;
  std::string problem;
};

// Each line that is not a comment is expect, a space, and a printed line.
Expected read_expected(const std::string &path) {
  Expected expected;
  std::ifstream file(path);
  if (!file) {
    return expected;
  }
  expected.exists = true;
  const std::string word = "expect";
  std::string line;
  for (std::size_t number = 1; std::getline(file, line); ++number) {
    const std::size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line.compare(start, 2, "//") == 0) {
      continue;
    }
    if (line.compare(0, word.size(), word) != 0 ||
        (line.size() > word.size() && line[word.size()] != ' ')) {
      expected.problem = path + ":" + std::to_string(number) +
                         ": a line that is not a comment starts with expect";
      return expected;
    }
    if (line.size() > word.size()) {
      expected.printed += line.substr(word.size() + 1);
    }
    expected.printed += '\n';
  }
  return expected;
}

// What a program prints is shown only when it fails.
bool passes(const char *tier, const Program &program) {
  const std::string stem = std::string("test/") + tier + '/' + program.name;
  std::ostringstream output;
  std::streambuf *const shown = std::cout.rdbuf(output.rdbuf());
  const int status = program.bosonoga_main();
  std::cout.rdbuf(shown);
  if (status != EXIT_SUCCESS) {
    std::cerr << "FAIL " << stem << ".bosonoga: returned exit_failure\n"
              << output.str();
    return false;
  }
  const Expected expected = read_expected(stem + ".xp.bosonoga");
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
  namespace program = bosonoga::program;
  const Tier tiers[] = {
      {"silver",
       {
           {"arithmetic", program::arithmetic},
           {"array", program::array},
           {"boolean", program::boolean},
           {"branch", program::branch},
           {"compare", program::compare},
           {"function", program::function},
           {"info", program::info},
           {"join", program::join},
           {"loop", program::loop},
           {"print", program::print},
           {"text", program::text},
       }},
      {"gold",
       {
           {"accumulate", program::accumulate},
           {"convert", program::convert},
           {"recursion", program::recursion},
           {"results", program::results},
           {"shift", program::shift},
       }},
      {"platinum", {}},
  };

  std::size_t passed = 0;
  std::size_t total = 0;
  std::ostringstream counts;
  for (const Tier &tier : tiers) {
    std::size_t tier_passed = 0;
    for (const Program &each : tier.programs) {
      tier_passed += passes(tier.name, each) ? 1 : 0;
    }
    counts << (&tier == tiers ? "" : " ") << tier.name << ' ' << tier_passed
           << '/' << tier.programs.size();
    passed += tier_passed;
    total += tier.programs.size();
  }
  std::cout << passed << '/' << total << " (" << counts.str() << ")\n\n";
  return total > 0 && passed == total ? EXIT_SUCCESS : EXIT_FAILURE;
}
