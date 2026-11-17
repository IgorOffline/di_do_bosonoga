// What a NAME.xp.bosonoga expects a program to print: each line after
// expect, as the C bosonoga's test runner reads it.
#ifndef BOSONOGA_TEST_EXPECTED_OUTPUT_HH
#define BOSONOGA_TEST_EXPECTED_OUTPUT_HH

#include <sys/stat.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <string>

namespace bosonoga::tests {

struct Expected {
  bool exists = false;
  std::string printed;
  std::string problem;
};

inline Expected read_expected(const std::string &path) {
  Expected expected;
  struct stat status{};
  // As the archive's existence check, a missing file has no expectations.
  if (::stat(path.c_str(), &status) != 0) {
    return expected;
  }
  expected.exists = true;
  std::ifstream file(path);
  if (!file) {
    expected.problem = path + ": " + std::strerror(errno);
    return expected;
  }
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
  if (file.bad()) {
    expected.problem = path + ": " + std::strerror(errno);
  }
  return expected;
}

}  // namespace bosonoga::tests

#endif
