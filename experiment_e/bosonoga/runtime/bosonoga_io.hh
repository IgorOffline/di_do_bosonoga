// io_read, io_write and io_modified, on POSIX, with the semantics of the
// GLib file functions that the C bosonoga uses.
#ifndef BOSONOGA_IO_HH
#define BOSONOGA_IO_HH

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "bosonoga_text.hh"

namespace bosonoga::runtime {

namespace io {

constexpr std::size_t file_name_max = 255;
constexpr std::size_t shown_max = 64;
constexpr const char *declaration =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";

inline bool is_file_name(const std::string &text) {
  if (text.empty() || text.size() > file_name_max || text[0] == '.') {
    return false;
  }
  for (const char c : text) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
      return false;
    }
  }
  return true;
}

// As g_build_filename of the directory and a file name, which has no /.
inline std::string path(const Where &where, const Dir &dir,
                        const std::string &name) {
  if (!is_file_name(name)) {
    fail(where,
         "'" + make_valid(name.substr(0, shown_max)) +
             "' is not a file name, which is 1 to " +
             std::to_string(file_name_max) +
             " ASCII letters, digits, _, - and ., and does not start with .");
  }
  const std::string directory = dir.path;
  std::size_t start = 0;
  while (start < directory.size() && directory[start] == '/') {
    ++start;
  }
  std::size_t end = directory.size();
  while (end > start && directory[end - 1] == '/') {
    --end;
  }
  std::string joined = directory.substr(0, end);
  if (end > start) {
    joined += '/';
  }
  return joined + name;
}

inline std::string reason(const std::string &path, const std::string &what) {
  return path + ": " + what;
}

inline bool read(const std::string &path, std::size_t max,
                 std::string &contents, std::string &why) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    why = reason(path, std::strerror(errno));
    return false;
  }
  struct stat status{};
  if (::fstat(fd, &status) != 0) {
    why = reason(path, std::strerror(errno));
    ::close(fd);
    return false;
  }
  if (!S_ISREG(status.st_mode)) {
    why = reason(path, "is not a file");
    ::close(fd);
    return false;
  }
  std::string text;
  std::vector<char> buffer(65536);
  while (true) {
    const ssize_t count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      why = reason(path, std::strerror(errno));
      ::close(fd);
      return false;
    }
    if (count == 0) {
      break;
    }
    text.append(buffer.data(), static_cast<std::size_t>(count));
    if (text.size() > max) {
      why = reason(path, "is longer than " + std::to_string(max) + " bytes");
      ::close(fd);
      return false;
    }
  }
  ::close(fd);
  contents = std::move(text);
  return true;
}

// As g_mkdir_with_parents, which makes each directory of the path that is not
// there.
inline bool make_directories(const std::string &directory) {
  std::size_t at = 0;
  while (at < directory.size() && directory[at] == '/') {
    ++at;
  }
  while (true) {
    while (at < directory.size() && directory[at] != '/') {
      ++at;
    }
    const std::string part = directory.substr(0, at);
    struct stat status{};
    if (::access(part.c_str(), F_OK) != 0) {
      if (::mkdir(part.c_str(), 0755) == -1 && errno != EEXIST) {
        return false;
      }
    } else if (::stat(part.c_str(), &status) != 0 || !S_ISDIR(status.st_mode)) {
      errno = ENOTDIR;
      return false;
    }
    if (at == directory.size()) {
      return true;
    }
    while (at < directory.size() && directory[at] == '/') {
      ++at;
    }
    if (at == directory.size()) {
      return true;
    }
  }
}

// Only makes a new file, so taken is true if it is there. A file that fails to
// be written is removed.
inline bool write(const std::string &directory, const std::string &path,
                  const std::string &text, bool &taken, std::string &why) {
  taken = false;
  if (!make_directories(directory)) {
    why = reason(directory, std::strerror(errno));
    return false;
  }
  const int fd =
      ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (fd < 0) {
    taken = errno == EEXIST;
    why = reason(path, std::strerror(errno));
    return false;
  }
  std::size_t written = 0;
  while (written < text.size()) {
    const ssize_t count =
        ::write(fd, text.data() + written, text.size() - written);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      why = reason(path, std::strerror(errno));
      ::close(fd);
      ::unlink(path.c_str());
      return false;
    }
    written += static_cast<std::size_t>(count);
  }
  if (::close(fd) != 0) {
    why = reason(path, std::strerror(errno));
    ::unlink(path.c_str());
    return false;
  }
  return true;
}

}  // namespace io

// Whether the file is read, its text, and why it is not. Only a text that the
// block takes is checked, as UTF-8 or as a document whose root it gives.
inline std::tuple<bool, std::string, std::string> io_read(
    const Where &where, const Dir &dir, const std::string &name,
    bool takes_text, bool is_xml) {
  const std::string path = io::path(where, dir, name);
  std::string contents;
  std::string why;
  bool is_read = io::read(path, xml_str_length_max, contents, why);
  std::size_t start = 0;
  std::size_t length = contents.size();
  if (is_read && takes_text) {
    if (!is_xml) {
      if (!is_utf8_text(contents)) {
        is_read = false;
        why = path + ": is not UTF-8 text without NUL bytes";
      }
    } else if (const auto problem =
                   markup::check_document(contents, start, length)) {
      is_read = false;
      why = path + ": is not an XML document: " + *problem;
    }
  }
  return {is_read, is_read ? contents.substr(start, length) : std::string(),
          why};
}

// Whether the file is written, whether it was there already, and why it is
// not written. An xml_str is written as a document, with a declaration.
inline std::tuple<bool, bool, std::string> io_write(const Where &where,
                                                    const Dir &dir,
                                                    const std::string &name,
                                                    const std::string &text,
                                                    bool is_xml) {
  const std::string path = io::path(where, dir, name);
  std::string written = text;
  if (is_xml) {
    if (!xml::is_element(text)) {
      fail(where,
           "io_write writes an xml_str of one element, the root of its "
           "document");
    }
    written = std::string(io::declaration) + "\n" + text + "\n";
  }
  bool is_taken = false;
  std::string why;
  const bool is_saved = io::write(dir.path, path, written, is_taken, why);
  return {is_saved, is_taken, why};
}

// Whether the file is there, and when it was modified, in nanoseconds since
// 1970.
inline std::tuple<bool, std::int64_t> io_modified(const Where &where,
                                                  const Dir &dir,
                                                  const std::string &name) {
  const std::string path = io::path(where, dir, name);
  struct stat status{};
  if (::stat(path.c_str(), &status) != 0 || !S_ISREG(status.st_mode)) {
    return {false, 0};
  }
  return {true, static_cast<std::int64_t>(status.st_mtim.tv_sec) * 1000000000 +
                    static_cast<std::int64_t>(status.st_mtim.tv_nsec)};
}

}  // namespace bosonoga::runtime

#endif
