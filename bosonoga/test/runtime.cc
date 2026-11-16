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

void gfx_scene_checks() {
  namespace runtime = bosonoga::runtime;
  namespace gfx = runtime::gfx;
  gfx::Scene scene;
  scene.delta_time = 0.25F;
  scene.key = runtime::InputKey{std::uint32_t{32}};
  scene.reset(runtime::Color{0x123456U});
  scene.rectangle(1.0F, 2.0F, -1.0F, 0.0F, runtime::Color{1});
  scene.hex(0.0F, 0.0F, 0.0F, runtime::Color{2});
  scene.circle(0.0F, 0.0F, -1.0F, runtime::Color{3});
  scene.hex(3.0F, 4.0F, 5.0F, runtime::Color{4});
  scene.draw_icon(1.0F, 2.0F, runtime::Icon{7, "unused", "unused"}, 0.5F, runtime::Color{5});
  scene.draw_text(0.0F, 0.0F, runtime::Font{0, "unused", "unused"}, 0.0F, runtime::Color{6}, 1, "skip");
  scene.draw_text(1.0F, 2.0F, runtime::Font{3, "unused", "unused"}, 12.0F, runtime::Color{7}, 66, "one");
  scene.draw_text(1.0F, 2.0F, runtime::Font{3, "unused", "unused"}, 12.0F, runtime::Color{7}, 66, "two");
  check(scene.shapes.size() == 5 && scene.shapes[0].kind == gfx::ShapeKind::rectangle
            && scene.shapes[1].kind == gfx::ShapeKind::hex && scene.shapes[2].icon == 7,
        "gfx scene skip rules or draw order differ");
  check(scene.text == "onetwo" && scene.shapes[4].text == 3 && scene.shapes[4].text_length == 3
            && scene.shapes[3].font == 3 && scene.shapes[3].align == 66,
        "gfx text storage or alignment differs");
  const runtime::Where first{"gfx.bosonoga", 1, 1}, second{"gfx.bosonoga", 2, 3};
  check(gfx::Text().part(first, std::string(1024, 'x')).take().size() == 1024, "gfx text limit is too small");
  try {
    gfx::Text().part(first, std::string(1024, 'x')).part(second, 1);
    check(false, "gfx text limit was not checked per part");
  } catch (const runtime::Error &error) {
    check(std::string(error.what()) == "gfx.bosonoga:2:3: gfx_draw_text is longer than 1024 bytes",
          "gfx text error location differs");
  }
  scene.clear(runtime::Color{9});
  check(scene.clear_color.rgb == 9 && scene.shapes.size() == 5, "gfx_clear discarded shapes");
  scene.reset(runtime::Color{10});
  check(scene.shapes.empty() && scene.text.empty() && scene.clear_color.rgb == 10
            && scene.delta_time == 0.25F && scene.key == runtime::InputKey{"SAPP_KEYCODE_SPACE"},
        "gfx frame reset or key codes differ");
  check(runtime::printed(runtime::InputKey{std::uint32_t{9999}}) == "?", "unknown key code needs ?");
  for (const auto &key : runtime::key_names) {
    check(runtime::InputKey{key.name} == runtime::InputKey{key.code}
              && runtime::printed(runtime::InputKey{key.code}) == key.name,
          "gfx key code/name table differs");
  }
}
}  // namespace

int main(int argc, char **argv) {
  gfx_scene_checks();
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
