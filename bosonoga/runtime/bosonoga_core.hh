// What every part of the runtime needs: runtime errors, which are thrown as
// an Error whose message starts with file:line:column, and the bounds of
// calls and of array rows.
#ifndef BOSONOGA_CORE_HH
#define BOSONOGA_CORE_HH

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

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

inline void sleep_ms(std::uint32_t milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

}  // namespace bosonoga::runtime

#endif
