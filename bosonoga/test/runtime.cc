#include "bosonoga_runtime.hh"
#include "expected_output.hh"

#include <clocale>
#include <iomanip>

namespace {
class GroupedNumbers : public std::numpunct<char> {
 protected:
  char do_decimal_point() const override { return ','; }
  char do_thousands_sep() const override { return '_'; }
  std::string do_grouping() const override { return "\3"; }
};

void check(bool passed, const char *message) {
  if (!passed) {
    throw std::runtime_error(message);
  }
}
}  // namespace

int main(int argc, char **argv) {
  namespace runtime = bosonoga::runtime;
  // An optional installed C locale also exercises comma-decimal formatting.
  if (argc > 1) {
    check(std::setlocale(LC_NUMERIC, argv[1]) != nullptr, "C locale unavailable");
  }
  const std::locale original;
  std::locale::global(std::locale(original, new GroupedNumbers));
  std::ostringstream output;
  output << std::hex << std::showbase << std::showpos;
  runtime::info(output, std::int32_t{-1234}, std::string{" "},
                std::numeric_limits<std::uint64_t>::max(), std::string{" "},
                1234.5F, std::string{" "}, 1234.5);
  check(output.str() == "-1234 18446744073709551615 1234d5 1234d5\n",
        "numeric output depends on the host format");
  const runtime::Where where{"locale.bosonoga", 1, 1};
  const auto text = runtime::NewStr().part(where, std::int64_t{1234567})
                        .part(where, std::string{" "}).part(where, 1.25).take();
  check(text == "1234567 1d25", "new str depends on the global locale");
  check(runtime::XmlJoin().text(where, std::uint32_t{1234567})
                .text(where, 1.25F).take() == "12345671d25",
        "XML numeric text depends on the global locale");
  check(runtime::real_text_6(-0.0) == "-0", "negative zero changed");
  check(runtime::real_text_6(0.00001234567) == "1d23457e-05",
        "f32 precision or exponent format changed");
  check(runtime::real_text_17(0.1) == "0d10000000000000001",
        "f64 precision changed");
  try {
    runtime::accumulate<float>(where, runtime::Step::multiply,
                              {std::numeric_limits<float>::max(), 2.0F});
    check(false, "overflow was accepted");
  } catch (const runtime::Error &error) {
    check(std::string(error.what()) ==
              "locale.bosonoga:1:1: the result (6d80565e+38) is outside the f32 range",
          "range diagnostic depends on the locale");
  }
  std::locale::global(original);
  char pattern[] = "/tmp/bosonoga-expected-XXXXXX";
  const char *created = ::mkdtemp(pattern);
  check(created != nullptr, "cannot make expectation test directory");
  const std::string directory = created;
  const std::string path = directory + "/output.xp.bosonoga";
  check(!bosonoga::tests::read_expected(path).exists, "missing expectations were treated as present");
  {
    std::ofstream file(path);
    file << "// comment\nexpect value\nexpect\n";
  }
  const auto expected = bosonoga::tests::read_expected(path);
  check(expected.exists && expected.problem.empty() && expected.printed == "value\n\n",
        "valid expectations changed");
  const auto unreadable = bosonoga::tests::read_expected(directory);
  check(unreadable.exists && !unreadable.problem.empty(),
        "existing unreadable expectations silently passed");
  {
    std::ofstream file(path);
    file << "invalid\n";
  }
  check(bosonoga::tests::read_expected(path).problem ==
            path + ":1: a line that is not a comment starts with expect",
        "malformed expectation diagnostic changed");
  check(::unlink(path.c_str()) == 0 && ::rmdir(directory.c_str()) == 0,
        "cannot remove expectation test files");
}
