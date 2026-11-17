// What makes and reads a str or an xml_str: new, to_join, xml_child,
// xml_attribute and str_parse.
#ifndef BOSONOGA_TEXT_HH
#define BOSONOGA_TEXT_HH

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <tuple>
#include <utility>

#include "bosonoga_markup.hh"
#include "bosonoga_value.hh"

namespace bosonoga::runtime {

constexpr std::size_t new_str_length_max = 1024;
constexpr std::size_t xml_str_length_max = 1048576;

// What new str of makes: the values as info prints them, with nothing between.
// Each is checked where it is written, as the C bosonoga joins one at a time.
class NewStr {
 public:
  template <typename T>
  NewStr &part(const Where &where, const T &value) {
    text_ += printed(value);
    if (text_.size() > new_str_length_max) {
      fail(where, "new str of is longer than " +
                      std::to_string(new_str_length_max) + " bytes");
    }
    return *this;
  }

  std::string take() { return std::move(text_); }

 private:
  std::string text_;
};

// As g_utf8_validate, and without NUL bytes.
inline bool is_utf8_text(const std::string &text) {
  return markup::is_valid(text);
}

// As g_utf8_make_valid, which puts U+FFFD for each byte that is not of a
// character.
inline std::string make_valid(const std::string &text) {
  std::string valid;
  for (std::size_t i = 0; i < text.size();) {
    const std::uint32_t code =
        markup::char_validated(text.data() + i, text.size() - i);
    if (code == markup::invalid || code == markup::incomplete) {
      valid += "\xEF\xBF\xBD";
      ++i;
      continue;
    }
    const std::size_t length =
        markup::char_length(static_cast<unsigned char>(text[i]));
    valid.append(text, i, length);
    i += length;
  }
  return valid;
}

// What new xml_str and to_join make, of parts that are each checked where they
// are written. A value that is not an xml_str is its text, escaped.
class XmlJoin {
 public:
  XmlJoin() = default;
  // As to_join, from what the xml_str holds.
  explicit XmlJoin(std::string held) : text_(std::move(held)) {}

  XmlJoin &raw(const Where &where, const std::string &xml) {
    text_ += xml;
    return checked(where);
  }

  template <typename T>
  XmlJoin &text(const Where &where, const T &shown) {
    escape(where, printed(shown));
    return checked(where);
  }

  // The value of an attribute, whose name opened its quote.
  template <typename T>
  XmlJoin &value(const Where &where, const T &shown) {
    escape(where, printed(shown));
    text_ += '"';
    return checked(where);
  }

  XmlJoin &open(const Where &where, const char *element) {
    text_ += '<';
    text_ += element;
    return checked(where);
  }

  XmlJoin &name(const Where &where, const char *attribute) {
    text_ += ' ';
    text_ += attribute;
    text_ += "=\"";
    return checked(where);
  }

  XmlJoin &start(const Where &where) {
    text_ += '>';
    return checked(where);
  }

  XmlJoin &empty(const Where &where) {
    text_ += "/>";
    return checked(where);
  }

  XmlJoin &close(const Where &where, const char *element) {
    text_ += "</";
    text_ += element;
    text_ += '>';
    return checked(where);
  }

  std::string take() { return std::move(text_); }

 private:
  void escape(const Where &where, const std::string &shown) {
    if (!is_utf8_text(shown)) {
      fail(where, "an xml_str holds UTF-8 text without NUL bytes");
    }
    for (const char character : shown) {
      switch (character) {
        case '&':
          text_ += "&amp;";
          break;
        case '<':
          text_ += "&lt;";
          break;
        case '>':
          text_ += "&gt;";
          break;
        case '"':
          text_ += "&quot;";
          break;
        case '\'':
          text_ += "&apos;";
          break;
        default:
          text_ += character;
          break;
      }
    }
  }

  XmlJoin &checked(const Where &where) {
    if (text_.size() > xml_str_length_max) {
      fail(where, "an xml_str is longer than " +
                      std::to_string(xml_str_length_max) + " bytes");
    }
    return *this;
  }

  std::string text_;
};

// How the C bosonoga reads an xml_str, which GMarkup checked: a tag ends at the
// first > outside its quotes.
namespace xml {

inline bool is_blank(char character) {
  return character == ' ' || character == '\t' || character == '\r' ||
         character == '\n';
}

// The offset after the > of the tag that starts at start.
inline std::size_t tag_end(const std::string &text, std::size_t start,
                           bool &is_empty) {
  for (std::size_t i = start + 1; i < text.size(); ++i) {
    if (text[i] == '"' || text[i] == '\'') {
      const char quote = text[i];
      ++i;
      while (i < text.size() && text[i] != quote) {
        ++i;
      }
    } else if (text[i] == '>') {
      is_empty = text[i - 1] == '/';
      return i + 1;
    }
  }
  return text.size();
}

inline std::size_t element_end(const std::string &text, std::size_t start) {
  bool is_empty = false;
  std::size_t i = tag_end(text, start, is_empty);
  if (is_empty) {
    return i;
  }
  std::uint32_t depth = 1;
  while (i < text.size()) {
    const std::size_t tag = text.find('<', i);
    if (tag == std::string::npos) {
      return text.size();
    }
    i = tag_end(text, tag, is_empty);
    if (tag + 1 < text.size() && text[tag + 1] == '/') {
      if (--depth == 0) {
        return i;
      }
    } else if (!is_empty) {
      ++depth;
    }
  }
  return text.size();
}

inline bool is_element(const std::string &text) {
  return !text.empty() && text[0] == '<' &&
         element_end(text, 0) == text.size();
}

inline std::size_t name_length(const std::string &text, std::size_t start) {
  std::size_t end = start;
  while (end < text.size() && !is_blank(text[end]) && text[end] != '/' &&
         text[end] != '>' && text[end] != '=') {
    ++end;
  }
  return end - start;
}

inline bool child(const std::string &text, const std::string &name,
                  std::uint32_t index, std::size_t &found_start,
                  std::size_t &found_length) {
  bool is_empty = false;
  std::size_t i = tag_end(text, 0, is_empty);
  if (is_empty) {
    return false;
  }
  std::uint32_t seen = 0;
  while (i < text.size()) {
    const std::size_t found = text.find('<', i);
    if (found == std::string::npos ||
        (found + 1 < text.size() && text[found + 1] == '/')) {
      return false;
    }
    const std::size_t end = element_end(text, found);
    if (name_length(text, found + 1) == name.size() &&
        text.compare(found + 1, name.size(), name) == 0) {
      if (seen == index) {
        found_start = found;
        found_length = end - found;
        return true;
      }
      ++seen;
    }
    i = end;
  }
  return false;
}

inline void append_utf8(std::string &out, std::uint32_t code) {
  if (code < 0x80) {
    out += static_cast<char>(code);
  } else if (code < 0x800) {
    out += static_cast<char>(0xC0U | (code >> 6));
    out += static_cast<char>(0x80U | (code & 0x3FU));
  } else if (code < 0x10000) {
    out += static_cast<char>(0xE0U | (code >> 12));
    out += static_cast<char>(0x80U | ((code >> 6) & 0x3FU));
    out += static_cast<char>(0x80U | (code & 0x3FU));
  } else {
    out += static_cast<char>(0xF0U | (code >> 18));
    out += static_cast<char>(0x80U | ((code >> 12) & 0x3FU));
    out += static_cast<char>(0x80U | ((code >> 6) & 0x3FU));
    out += static_cast<char>(0x80U | (code & 0x3FU));
  }
}

// Of an entity or a character reference that starts after its & at from, and
// ends with a ; before end. It returns how much it read after the &.
inline std::size_t unescape_one(const std::string &text, std::size_t from,
                                std::size_t end, std::string &value) {
  const std::size_t semicolon = text.find(';', from);
  const std::size_t length = (semicolon < end ? semicolon : end) - from;
  static const struct {
    const char *name;
    char character;
  } entities[] = {
      {"lt", '<'}, {"gt", '>'}, {"amp", '&'}, {"quot", '"'}, {"apos", '\''},
  };
  for (const auto &entity : entities) {
    if (text.compare(from, length, entity.name) == 0) {
      value += entity.character;
      return length + 1;
    }
  }
  const bool is_hex = text[from + 1] == 'x';
  const std::size_t skipped = is_hex ? 2 : 1;
  const std::string digits = text.substr(from + skipped, length - skipped);
  append_utf8(value, static_cast<std::uint32_t>(
                         std::strtoull(digits.c_str(), nullptr, is_hex ? 16 : 10)));
  return length + 1;
}

inline bool attribute(const std::string &text, const std::string &name,
                      std::string &value) {
  bool is_empty = false;
  const std::size_t end_of_tag = tag_end(text, 0, is_empty);
  std::size_t i = 1 + name_length(text, 1);
  while (i < end_of_tag) {
    while (is_blank(text[i])) {
      ++i;
    }
    if (text[i] == '/' || text[i] == '>') {
      return false;
    }
    const std::size_t start = i;
    const std::size_t length = name_length(text, i);
    i += length;
    while (text[i] != '"' && text[i] != '\'') {
      ++i;
    }
    const char quote = text[i++];
    const std::size_t value_start = i;
    while (text[i] != quote) {
      ++i;
    }
    const std::size_t value_end = i++;
    if (length != name.size() || text.compare(start, length, name) != 0) {
      continue;
    }
    for (std::size_t j = value_start; j < value_end; ++j) {
      if (text[j] == '&') {
        j += unescape_one(text, j + 1, value_end, value);
      } else if (text[j] == '\t' || text[j] == '\n' || text[j] == '\r') {
        value += ' ';
      } else {
        value += text[j];
      }
    }
    return true;
  }
  return false;
}

}  // namespace xml

// The element at index among the children of the element that xml holds that
// have the name, and whether there is one.
inline std::tuple<bool, std::string> xml_child(const Where &where,
                                               const std::string &text,
                                               const std::string &name,
                                               std::int32_t index) {
  if (!xml::is_element(text)) {
    fail(where, "xml_child reads an xml_str of one element");
  }
  if (index < 0) {
    fail(where, "xml_child takes an index from 0, not " + std::to_string(index));
  }
  std::size_t start = 0;
  std::size_t length = 0;
  const bool is_found =
      xml::child(text, name, static_cast<std::uint32_t>(index), start, length);
  return {is_found, text.substr(start, length)};
}

inline std::tuple<bool, std::string> xml_attribute(const Where &where,
                                                   const std::string &text,
                                                   const std::string &name) {
  if (!xml::is_element(text)) {
    fail(where, "xml_attribute reads an xml_str of one element");
  }
  std::string value;
  const bool is_found = xml::attribute(text, name, value);
  return {is_found, value};
}

// A decimal integer, without a + or leading zeros, that fits T.
template <typename T>
std::tuple<bool, T> str_parse(const std::string &text) {
  const bool is_negative = !text.empty() && text[0] == '-';
  const std::size_t first = is_negative ? 1 : 0;
  const std::size_t digits = text.size() - first;
  constexpr std::size_t digits_max = 20;
  bool is_parsed = digits > 0 && !(digits > 1 && text[first] == '0');
  for (std::size_t i = first; is_parsed && i < text.size(); ++i) {
    is_parsed = text[i] >= '0' && text[i] <= '9';
  }
  if (!is_parsed || digits > digits_max) {
    return {false, T{0}};
  }
  Wide whole = 0;
  for (std::size_t i = first; i < text.size(); ++i) {
    whole = whole * 10 + (text[i] - '0');
  }
  // As the C bosonoga, a negative number fits an i64 and a positive one a u64.
  const Wide limit = is_negative
                         ? -static_cast<Wide>(std::numeric_limits<std::int64_t>::min())
                         : static_cast<Wide>(std::numeric_limits<std::uint64_t>::max());
  if (whole > limit) {
    return {false, T{0}};
  }
  whole = is_negative ? -whole : whole;
  if (!whole_fits<T>(whole)) {
    return {false, T{0}};
  }
  return {true, static_cast<T>(whole)};
}

}  // namespace bosonoga::runtime

#endif
