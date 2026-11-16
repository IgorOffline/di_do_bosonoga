// The values that are not numbers, and how info prints every value.
#ifndef BOSONOGA_VALUE_HH
#define BOSONOGA_VALUE_HH

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>

#include "bosonoga_number.hh"

namespace bosonoga::runtime {

template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>,
                                      int> = 0>
void print(std::ostream &out, T value) {
  // Bosonoga always prints decimal digits, even if its host uses hex,
  // showbase, showpos or a locale with digit grouping on this stream.
  out << wide_text(static_cast<Wide>(value));
}

inline void print(std::ostream &out, float value) {
  out << real_text_6(static_cast<double>(value));
}

inline void print(std::ostream &out, double value) {
  out << real_text_17(value);
}

inline void print(std::ostream &out, bool value) {
  out << (value ? "true" : "false");
}

inline void print(std::ostream &out, const std::string &value) { out << value; }

// A color of regina.ini, as 0xRRGGBB, which a zeroed row has as black. It is
// not a number, so that it has neither arithmetic nor comparisons.
struct Color {
  std::uint32_t rgb;
};

inline void print(std::ostream &out, Color value) {
  char text[16];
  std::snprintf(text, sizeof text, "%06X", static_cast<unsigned>(value.rgb));
  out << text;
}

// A key of regina.ini, as its name of sokol_app.
struct InputKey {
  const char *name;
};

inline bool operator==(InputKey a, InputKey b) {
  return std::strcmp(a.name, b.name) == 0;
}
inline bool operator!=(InputKey a, InputKey b) { return !(a == b); }
inline void print(std::ostream &out, InputKey value) { out << value.name; }

// A font, an icon or a directory of regina.ini, numbered by the program, as
// the text of regina.ini names it and as its path from the directory of
// regina.ini.
template <int Group>
struct Asset {
  std::uint32_t index;
  const char *text;
  const char *path;
};

template <int Group>
bool operator==(Asset<Group> a, Asset<Group> b) {
  return a.index == b.index;
}

template <int Group>
bool operator!=(Asset<Group> a, Asset<Group> b) {
  return !(a == b);
}

template <int Group>
void print(std::ostream &out, Asset<Group> value) {
  out << value.text;
}

using Font = Asset<0>;
using Icon = Asset<1>;
using Dir = Asset<2>;

// The values are printed with nothing between them.
template <typename... Values>
void info(std::ostream &out, const Values &...values) {
  (print(out, values), ...);
  out << '\n';
}

template <typename T>
std::string printed(const T &value) {
  std::ostringstream out;
  print(out, value);
  return out.str();
}

}  // namespace bosonoga::runtime

#endif
