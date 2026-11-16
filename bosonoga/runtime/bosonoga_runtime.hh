// What the C++ generated from bosonoga needs at run time, with the semantics of
// the C bosonoga: arithmetic is checked, and a runtime error is thrown as an
// Error whose message starts with file:line:column.
#ifndef BOSONOGA_RUNTIME_HH
#define BOSONOGA_RUNTIME_HH

#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

namespace bosonoga::runtime {

// Holds every integer type exactly, and the values in between up to 2^127.
__extension__ typedef __int128 Wide;

class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct Where {
  const char *source;
  int line;
  int column;
};

[[noreturn]] inline void fail(const Where &where, const std::string &message) {
  throw Error(std::string(where.source) + ':' + std::to_string(where.line) +
              ':' + std::to_string(where.column) + ": " + message);
}

constexpr int call_depth_max = 10000;

inline int &call_depth() {
  static int depth = 0;
  return depth;
}

// Each generated function holds one while it runs, as the C bosonoga bounds how
// deep calls are nested.
class Call {
 public:
  explicit Call(const Where &where) {
    if (call_depth() == call_depth_max) {
      fail(where, "calls are nested more than " +
                      std::to_string(call_depth_max) + " deep");
    }
    ++call_depth();
  }
  ~Call() { --call_depth(); }
  Call(const Call &) = delete;
  Call &operator=(const Call &) = delete;
};

template <typename T>
constexpr const char *kind_name() {
  if constexpr (std::is_same_v<T, std::int32_t>) {
    return "i32";
  } else if constexpr (std::is_same_v<T, std::int64_t>) {
    return "i64";
  } else if constexpr (std::is_same_v<T, std::uint32_t>) {
    return "u32";
  } else if constexpr (std::is_same_v<T, std::uint64_t>) {
    return "u64";
  } else if constexpr (std::is_same_v<T, float>) {
    return "f32";
  } else {
    static_assert(std::is_same_v<T, double>, "not a bosonoga number");
    return "f64";
  }
}

// The decimal separator of bosonoga is the letter d.
inline std::string with_d(std::string text) {
  for (char &character : text) {
    if (character == '.') {
      character = 'd';
    }
  }
  return text;
}

inline std::string real_text_6(double value) {
  char text[64];
  std::snprintf(text, sizeof text, "%.6g", value);
  return with_d(text);
}

inline std::string real_text_17(double value) {
  char text[64];
  std::snprintf(text, sizeof text, "%.17g", value);
  return with_d(text);
}

// No printf format takes a Wide.
inline std::string wide_text(Wide value) {
  if (value == 0) {
    return "0";
  }
  const bool is_negative = value < 0;
  std::string digits;
  while (value != 0) {
    const Wide digit = value % 10;
    digits.insert(digits.begin(),
                  static_cast<char>('0' + (is_negative ? -digit : digit)));
    value /= 10;
  }
  return is_negative ? '-' + digits : digits;
}

template <typename T>
constexpr bool whole_fits(Wide value) {
  return value >= static_cast<Wide>(std::numeric_limits<T>::min()) &&
         value <= static_cast<Wide>(std::numeric_limits<T>::max());
}

// Written so that NaN is outside the range too.
template <typename T>
bool real_fits(double value) {
  if constexpr (std::is_same_v<T, float>) {
    return std::fabs(value) <= static_cast<double>(FLT_MAX);
  } else {
    return std::isfinite(value);
  }
}

template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>,
                                      int> = 0>
void print(std::ostream &out, T value) {
  out << value;
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

// The values are printed with nothing between them.
template <typename... Values>
void info(std::ostream &out, const Values &...values) {
  (print(out, values), ...);
  out << '\n';
}

// What new str of makes: the values as info prints them, with nothing between.
template <typename... Values>
std::string joined(const Values &...values) {
  std::ostringstream out;
  (print(out, values), ...);
  return out.str();
}

// What new made is freed when it is unset.
inline void unset(std::string &value) { std::string().swap(value); }

// A row of an array, as the C bosonoga reads and writes it, at a checked index.
template <typename Rows>
auto &row(const Where &where, Rows &rows, const char *name, std::int32_t index) {
  if (index < 0 || static_cast<std::size_t>(index) >= rows.size()) {
    if (rows.size() == 1) {
      fail(where, std::string("'") + name + "' has only row 0, not " +
                      std::to_string(index));
    }
    fail(where, std::string("'") + name + "' has rows 0 to " +
                    std::to_string(rows.size() - 1) + ", not " +
                    std::to_string(index));
  }
  return rows[static_cast<std::size_t>(index)];
}

inline bool close(double a, double b, double tolerance) {
  const double difference = std::fabs(a - b);
  const double larger =
      std::fabs(a) > std::fabs(b) ? std::fabs(a) : std::fabs(b);
  return difference <= tolerance ||
         (larger > 0.0 && difference / larger <= tolerance);
}

inline void sleep_ms(std::uint32_t milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

enum class Step { add, subtract, multiply, divide, mod };

// Each step is checked, as the accumulator itself can overflow.
inline bool whole_step(Wide &whole, Step step, Wide value, bool &by_zero) {
  switch (step) {
    case Step::add:
      return !__builtin_add_overflow(whole, value, &whole);
    case Step::subtract:
      return !__builtin_sub_overflow(whole, value, &whole);
    case Step::multiply:
      return !__builtin_mul_overflow(whole, value, &whole);
    case Step::divide:
    case Step::mod:
      by_zero = value == 0;
      if (by_zero) {
        return false;
      }
      // The smallest Wide divided by -1 does not fit.
      if (value == -1) {
        if (step == Step::mod) {
          whole = 0;
          return true;
        }
        return !__builtin_sub_overflow(Wide{0}, whole, &whole);
      }
      whole = step == Step::mod ? whole % value : whole / value;
      return true;
  }
  return false;
}

// One value of to_subtract or to_divide is subtracted from 0 or divides 1.
template <typename T>
T accumulate(const Where &where, Step step, std::initializer_list<T> values) {
  const bool is_inverse = values.size() == 1 &&
                          (step == Step::subtract || step == Step::divide);
  const T *next = is_inverse ? values.begin() : values.begin() + 1;
  if constexpr (std::is_integral_v<T>) {
    Wide whole = is_inverse ? Wide{step == Step::divide ? 1 : 0}
                            : static_cast<Wide>(*values.begin());
    for (; next != values.end(); ++next) {
      bool by_zero = false;
      if (!whole_step(whole, step, static_cast<Wide>(*next), by_zero)) {
        fail(where, by_zero ? std::string("division by zero")
                            : std::string("the result is outside the ") +
                                  kind_name<T>() + " range");
      }
    }
    if (!whole_fits<T>(whole)) {
      fail(where, "the result (" + wide_text(whole) + ") is outside the " +
                      kind_name<T>() + " range");
    }
    return static_cast<T>(whole);
  } else {
    double real = is_inverse ? (step == Step::divide ? 1.0 : 0.0)
                             : static_cast<double>(*values.begin());
    for (; next != values.end(); ++next) {
      const double value = static_cast<double>(*next);
      switch (step) {
        case Step::add:
          real += value;
          break;
        case Step::subtract:
          real -= value;
          break;
        case Step::multiply:
          real *= value;
          break;
        case Step::divide:
        case Step::mod:
          if (!(value < 0.0 || value > 0.0)) {
            fail(where, "division by zero");
          }
          real = step == Step::mod ? std::fmod(real, value) : real / value;
          break;
      }
    }
    if (!real_fits<T>(real)) {
      fail(where, "the result (" + real_text_6(real) + ") is outside the " +
                      kind_name<T>() + " range");
    }
    return static_cast<T>(real);
  }
}

// A real converts to an integer by dropping its fraction.
template <typename To, typename From>
To convert(const Where &where, From value) {
  const std::string range =
      std::string("the value is outside the ") + kind_name<To>() + " range";
  if constexpr (std::is_floating_point_v<To>) {
    const double real = static_cast<double>(value);
    if (!real_fits<To>(real)) {
      fail(where, range);
    }
    return static_cast<To>(real);
  } else if constexpr (std::is_integral_v<From>) {
    const Wide whole = static_cast<Wide>(value);
    if (!whole_fits<To>(whole)) {
      fail(where, range);
    }
    return static_cast<To>(whole);
  } else {
    // Every integer type is inside 2^65, and every real inside it converts
    // exactly.
    const double whole = std::trunc(static_cast<double>(value));
    if (!(std::fabs(whole) < 0x1p65) ||
        !whole_fits<To>(static_cast<Wide>(whole))) {
      fail(where, range);
    }
    return static_cast<To>(static_cast<Wide>(whole));
  }
}

enum class Math {
  abs,
  min,
  max,
  sin,
  cos,
  tan,
  asin,
  acos,
  atan2,
  sqrt,
  pow,
  hypot,
  floor,
  ceil,
  round,
  log,
  log10,
  log1p,
  exp,
  expm1,
};

inline const char *domain_error(Math function, double a, double b) {
  switch (function) {
    case Math::asin:
      return a < -1.0 || a > 1.0 ? "math_to_asin needs a value from -1 to 1"
                                 : nullptr;
    case Math::acos:
      return a < -1.0 || a > 1.0 ? "math_to_acos needs a value from -1 to 1"
                                 : nullptr;
    case Math::sqrt:
      return a < 0.0 ? "math_to_sqrt of a negative number" : nullptr;
    case Math::pow:
      if (a < 0.0 && std::trunc(b) != b) {
        return "math_to_pow of a negative number to a fractional power";
      }
      if (!(a < 0.0 || a > 0.0) && b < 0.0) {
        return "math_to_pow of zero to a negative power is division by zero";
      }
      return nullptr;
    case Math::log:
    case Math::log10:
      return a <= 0.0 ? "logarithms need a positive value" : nullptr;
    case Math::log1p:
      return a <= -1.0 ? "math_to_log1p needs a value greater than -1"
                       : nullptr;
    default:
      return nullptr;
  }
}

inline double real_function(Math function, double a, double b) {
  switch (function) {
    case Math::abs:
      return std::fabs(a);
    case Math::min:
      return std::fmin(a, b);
    case Math::max:
      return std::fmax(a, b);
    case Math::sin:
      return std::sin(a);
    case Math::cos:
      return std::cos(a);
    case Math::tan:
      return std::tan(a);
    case Math::asin:
      return std::asin(a);
    case Math::acos:
      return std::acos(a);
    case Math::atan2:
      return std::atan2(a, b);
    case Math::sqrt:
      return std::sqrt(a);
    case Math::pow:
      return std::pow(a, b);
    case Math::hypot:
      return std::hypot(a, b);
    case Math::floor:
      return std::floor(a);
    case Math::ceil:
      return std::ceil(a);
    case Math::round:
      return std::round(a);
    case Math::log:
      return std::log(a);
    case Math::log10:
      return std::log10(a);
    case Math::log1p:
      return std::log1p(a);
    case Math::exp:
      return std::exp(a);
    case Math::expm1:
      return std::expm1(a);
  }
  return std::nan("");
}

// An integer takes only abs, min and max, which are not through double, as it
// cannot hold every i64 and u64.
template <typename T>
T math(const Where &where, Math function, T a, T b = T{}) {
  const std::string range =
      std::string("the result is outside the ") + kind_name<T>() + " range";
  if constexpr (std::is_integral_v<T>) {
    const Wide x = static_cast<Wide>(a);
    const Wide y = static_cast<Wide>(b);
    Wide whole = x;
    if (function == Math::abs) {
      whole = x < 0 ? -x : x;
    } else if (function == Math::min) {
      whole = x < y ? x : y;
    } else if (function == Math::max) {
      whole = x > y ? x : y;
    }
    if (!whole_fits<T>(whole)) {
      fail(where, range);
    }
    return static_cast<T>(whole);
  } else {
    const double x = static_cast<double>(a);
    const double y = static_cast<double>(b);
    if (const char *domain = domain_error(function, x, y)) {
      fail(where, domain);
    }
    const double result = real_function(function, x, y);
    if (!real_fits<T>(result)) {
      fail(where, range);
    }
    return static_cast<T>(result);
  }
}

}  // namespace bosonoga::runtime

#endif
