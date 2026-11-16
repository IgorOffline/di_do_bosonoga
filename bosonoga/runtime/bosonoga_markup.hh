// GLib's GMarkup parser, which the C bosonoga checks a document that io_read
// reads with, so that a file is accepted and refused the same, with the same
// messages and the same line and char in them.
#ifndef BOSONOGA_MARKUP_HH
#define BOSONOGA_MARKUP_HH

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bosonoga_letters.hh"

namespace bosonoga::runtime::markup {

constexpr std::uint32_t invalid = 0xFFFFFFFFU;
constexpr std::uint32_t incomplete = 0xFFFFFFFEU;

// As g_utf8_get_char_validated, of at most available bytes.
inline std::uint32_t char_validated(const char *text, std::size_t available) {
  if (available == 0) {
    return incomplete;
  }
  const auto lead = static_cast<unsigned char>(text[0]);
  if (lead < 0x80) {
    return lead == 0 ? incomplete : lead;
  }
  std::size_t length = 0;
  std::uint32_t code = 0;
  std::uint32_t least = 0;
  if (lead < 0xC0) {
    return invalid;
  } else if (lead < 0xE0) {
    length = 2;
    code = lead & 0x1FU;
    least = 0x80;
  } else if (lead < 0xF0) {
    length = 3;
    code = lead & 0x0FU;
    least = 0x800;
  } else if (lead < 0xF8) {
    length = 4;
    code = lead & 0x07U;
    least = 0x10000;
  } else if (lead < 0xFC) {
    length = 5;
    code = lead & 0x03U;
    least = 0x200000;
  } else if (lead < 0xFE) {
    length = 6;
    code = lead & 0x01U;
    least = 0x4000000;
  } else {
    return invalid;
  }
  if (available < length) {
    for (std::size_t i = 1; i < available; ++i) {
      if ((static_cast<unsigned char>(text[i]) & 0xC0U) != 0x80U) {
        return invalid;
      }
    }
    return incomplete;
  }
  for (std::size_t i = 1; i < length; ++i) {
    const auto next = static_cast<unsigned char>(text[i]);
    if ((next & 0xC0U) != 0x80U) {
      return invalid;
    }
    code = (code << 6) | (next & 0x3FU);
  }
  if (code < least || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
    return invalid;
  }
  return code;
}

// The bytes of the character that starts with lead, as g_utf8_next_char.
inline std::size_t char_length(unsigned char lead) {
  if (lead < 0xC0) {
    return 1;
  }
  if (lead < 0xE0) {
    return 2;
  }
  if (lead < 0xF0) {
    return 3;
  }
  if (lead < 0xF8) {
    return 4;
  }
  return lead < 0xFC ? std::size_t{5} : std::size_t{6};
}

// As g_utf8_validate_len, which also refuses a NUL byte.
inline bool is_valid(const std::string &text) {
  for (std::size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i]);
    if (lead == 0 || (lead >= 0x80 && (lead < 0xC2 || lead > 0xF4))) {
      return false;
    }
    const std::uint32_t code = char_validated(text.data() + i, text.size() - i);
    if (code == invalid || code == incomplete) {
      return false;
    }
    i += char_length(lead);
  }
  return true;
}

inline bool is_ascii_letter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool is_common_name_end(char c) {
  return c == '=' || c == '/' || c == '>' || c == ' ';
}

inline bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

class Failure {
 public:
  explicit Failure(std::string text) : message(std::move(text)) {}
  std::string message;
};

// What the C bosonoga does with what GMarkup reads, which can refuse it.
class Handler {
 public:
  virtual ~Handler() = default;
  virtual void start(const std::string &element,
                     const std::vector<std::string> &attributes) = 0;
  virtual void end() = 0;
  virtual void passthrough(const std::string &text) = 0;
};

class Parser {
 public:
  Parser(const std::string &text, Handler &handler)
      : text_(text), handler_(handler) {}

  // As g_markup_parse_context_parse with the whole text, and then
  // g_markup_parse_context_end_parse.
  void parse() {
    while (iter_ != text_.size()) {
      step();
    }
    end_parse();
  }

 private:
  enum class State {
    initial,
    after_bom1,
    after_bom2,
    start,
    after_open_angle,
    after_close_angle,
    after_elision_slash,
    inside_open_tag_name,
    inside_attribute_name,
    after_attribute_name,
    between_attributes,
    after_attribute_equals_sign,
    inside_attribute_value_sq,
    inside_attribute_value_dq,
    inside_text,
    after_close_tag_slash,
    inside_close_tag_name,
    after_close_tag_name,
    inside_passthrough,
  };

  static constexpr std::size_t attributes_max = 1000;

  const std::string &text_;
  Handler &handler_;
  std::size_t iter_ = 0;
  std::size_t start_ = 0;
  std::size_t lines_ = 1;
  std::size_t chars_ = 1;
  State state_ = State::initial;
  std::string partial_;
  std::vector<std::string> tags_;
  std::vector<std::string> attributes_;
  bool is_document_empty_ = true;
  int balance_ = 0;

  [[noreturn]] void fail(const std::string &message) const {
    throw Failure("Error on line " + std::to_string(lines_) + " char " +
                  std::to_string(chars_) + ": " + message);
  }

  char at() const { return text_[iter_]; }

  bool advance() {
    ++iter_;
    ++chars_;
    if (iter_ == text_.size()) {
      return false;
    }
    if (text_[iter_] == '\n') {
      ++lines_;
      chars_ = 1;
    }
    return true;
  }

  void skip_spaces() {
    do {
      if (!is_space(at())) {
        return;
      }
    } while (advance());
  }

  void advance_to_name_end() {
    do {
      if (is_common_name_end(at()) || is_space(at())) {
        return;
      }
    } while (advance());
  }

  void add_to_partial() { partial_.append(text_, start_, iter_ - start_); }

  // The character at iter, for a message, or its byte as \x and hex digits.
  std::string character() const {
    const std::uint32_t code =
        char_validated(text_.data() + iter_, text_.size() - iter_);
    if (code == invalid || code == incomplete) {
      char shown[8];
      std::snprintf(shown, sizeof shown, "\\x%02x",
                    static_cast<unsigned>(static_cast<unsigned char>(at())));
      return shown;
    }
    return text_.substr(iter_, char_length(static_cast<unsigned char>(at())));
  }

  const std::string &current() const { return tags_.back(); }

  void validate_name(const std::string &name) const {
    if (!is_valid(name)) {
      fail("Invalid UTF-8 encoded text in name — not valid “" + name + "”");
    }
    const char first = name.empty() ? '\0' : name[0];
    const auto is_letter = [&name](std::size_t i) {
      return letters::is_letter(
          char_validated(name.data() + i, name.size() - i));
    };
    if (!(is_ascii_letter(first) ||
          (!is_common_name_end(first) && (first == '_' || first == ':' ||
                                          (!name.empty() && is_letter(0)))))) {
      fail("“" + name + "” is not a valid name");
    }
    for (std::size_t i = char_length(static_cast<unsigned char>(first));
         i < name.size();
         i += char_length(static_cast<unsigned char>(name[i]))) {
      const char c = name[i];
      if (!(is_ascii_letter(c) || (c >= '0' && c <= '9') ||
            (!is_common_name_end(c) &&
             (c == '.' || c == '-' || c == '_' || c == ':' || is_letter(i))))) {
        fail("“" + name + "” is not a valid name: “" + std::string(1, c) + "”");
      }
    }
  }

  void validate_text(const std::string &text) const {
    if (!is_valid(text)) {
      fail("Invalid UTF-8 encoded text in name — not valid “" + text + "”");
    }
  }

  // An unescape error names the line where from is, without its char.
  [[noreturn]] void fail_unescape(const std::string &chunk, std::size_t from,
                                  const std::string &message) const {
    std::size_t newlines = 0;
    for (std::size_t i = from; i < chunk.size(); ++i) {
      newlines += chunk[i] == '\n' ? 1 : 0;
    }
    throw Failure("Error on line " + std::to_string(lines_ - newlines) + ": " +
                  message);
  }

  // As unescape_gstring_inplace and text_validate, which check the entities
  // and the character references of a text or of the value of an attribute.
  void unescape(const std::string &chunk, bool is_attribute) const {
    std::string out;
    bool is_ascii = true;
    const char *text = chunk.c_str();
    for (std::size_t from = 0; from < chunk.size(); ++from) {
      char c = text[from];
      is_ascii = is_ascii && static_cast<unsigned char>(c) < 0x80;
      if (is_attribute && (c == '\t' || c == '\n')) {
        c = ' ';
      }
      if (c == '\r') {
        c = is_attribute ? ' ' : '\n';
        if (text[from + 1] == '\n') {
          ++from;
        }
      }
      if (text[from] != '&') {
        out += c;
        continue;
      }
      ++from;
      if (text[from] == '#') {
        int base = 10;
        ++from;
        if (text[from] == 'x') {
          base = 16;
          ++from;
        }
        errno = 0;
        char *end = nullptr;
        const unsigned long code = std::strtoul(text + from, &end, base);
        const auto digits = static_cast<std::size_t>(end - (text + from));
        if (digits == 0 || errno != 0) {
          fail_unescape(chunk, from,
                        "Failed to parse “" + chunk.substr(from, digits) +
                            "”, which should have been a digit inside a "
                            "character reference (&#234; for "
                            "example) — perhaps the digit is too large");
        }
        if (*end != ';') {
          fail_unescape(chunk, from,
                        "Character reference did not end with a semicolon; "
                        "most likely you used an ampersand "
                        "character without intending to start an entity — "
                        "escape ampersand as &amp;");
        }
        if (!((0 < code && code <= 0xD7FF) ||
              (0xE000 <= code && code <= 0xFFFD) ||
              (0x10000 <= code && code <= 0x10FFFF))) {
          fail_unescape(chunk, from,
                        "Character reference “" + chunk.substr(from, digits) +
                            "” does not encode a permitted character");
        }
        append_utf8(out, static_cast<std::uint32_t>(code));
        is_ascii = is_ascii && code < 0x80;
        from = static_cast<std::size_t>(end - text);
      } else if (std::strncmp(text + from, "lt;", 3) == 0) {
        out += '<';
        from += 2;
      } else if (std::strncmp(text + from, "gt;", 3) == 0) {
        out += '>';
        from += 2;
      } else if (std::strncmp(text + from, "amp;", 4) == 0) {
        out += '&';
        from += 3;
      } else if (std::strncmp(text + from, "quot;", 5) == 0) {
        out += '"';
        from += 4;
      } else if (std::strncmp(text + from, "apos;", 5) == 0) {
        out += '\'';
        from += 4;
      } else if (text[from] == ';') {
        fail_unescape(chunk, from,
                      "Empty entity “&;” seen; valid entities are: &amp; "
                      "&quot; &lt; &gt; &apos;");
      } else {
        const char *end = std::strchr(text + from, ';');
        if (end != nullptr) {
          fail_unescape(chunk, from,
                        "Entity name “" +
                            chunk.substr(from, static_cast<std::size_t>(
                                                   end - (text + from))) +
                            "” is not known");
        }
        fail_unescape(
            chunk, from,
            "Entity did not end with a semicolon; most likely you used an "
            "ampersand character "
            "without intending to start an entity — escape ampersand as &amp;");
      }
    }
    if (!is_ascii) {
      validate_text(out);
    }
  }

  static void append_utf8(std::string &out, std::uint32_t code) {
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

  void emit_start_element() {
    validate_name(current());
    handler_.start(current(), attributes_);
    attributes_.clear();
  }

  void emit_end_element() {
    handler_.end();
    tags_.pop_back();
  }

  void step() {
    switch (state_) {
      case State::initial:
        if (static_cast<unsigned char>(at()) == 0xEF) {
          advance();
          state_ = State::after_bom1;
        } else {
          state_ = State::start;
        }
        break;
      case State::after_bom1:
      case State::after_bom2:
        if (static_cast<unsigned char>(at()) !=
            (state_ == State::after_bom1 ? 0xBB : 0xBF)) {
          fail("Invalid byte order mark");
        }
        advance();
        state_ = state_ == State::after_bom1 ? State::after_bom2 : State::start;
        break;
      case State::start:
        skip_spaces();
        if (iter_ != text_.size()) {
          if (at() != '<') {
            fail("Document must begin with an element (e.g. <book>)");
          }
          advance();
          state_ = State::after_open_angle;
          start_ = iter_;
          is_document_empty_ = false;
        }
        break;
      case State::after_open_angle:
        if (at() == '?' || at() == '!') {
          partial_ += '<';
          start_ = iter_;
          balance_ = 1;
          state_ = State::inside_passthrough;
        } else if (at() == '/') {
          advance();
          state_ = State::after_close_tag_slash;
        } else if (!is_common_name_end(at())) {
          state_ = State::inside_open_tag_name;
          start_ = iter_;
        } else {
          fail("“" + character() +
               "” is not a valid character following a “<” character; it may "
               "not begin an "
               "element name");
        }
        break;
      case State::after_close_angle:
        state_ = tags_.empty() ? State::start : State::inside_text;
        start_ = iter_;
        break;
      case State::after_elision_slash:
        if (at() != '>') {
          fail("Odd character “" + character() +
               "”, expected a “>” character to end the empty-element tag “" +
               current() + "”");
        }
        advance();
        state_ = State::after_close_angle;
        emit_end_element();
        break;
      case State::inside_open_tag_name:
        advance_to_name_end();
        add_to_partial();
        if (iter_ != text_.size()) {
          tags_.push_back(partial_);
          partial_.clear();
          state_ = State::between_attributes;
        }
        break;
      case State::inside_attribute_name:
        advance_to_name_end();
        add_to_partial();
        if (iter_ != text_.size()) {
          state_ = State::after_attribute_name;
        }
        break;
      case State::after_attribute_name:
        skip_spaces();
        if (iter_ != text_.size()) {
          validate_name(partial_);
          if (attributes_.size() > attributes_max) {
            fail("Too many attributes in element “" + current() + "”");
          }
          attributes_.push_back(partial_);
          partial_.clear();
          if (at() != '=') {
            fail("Odd character “" + character() +
                 "”, expected a “=” after attribute name “" +
                 attributes_.back() + "” of element “" + current() + "”");
          }
          advance();
          state_ = State::after_attribute_equals_sign;
        }
        break;
      case State::between_attributes:
        skip_spaces();
        if (iter_ != text_.size()) {
          if (at() == '/') {
            advance();
            state_ = State::after_elision_slash;
          } else if (at() == '>') {
            advance();
            state_ = State::after_close_angle;
          } else if (!is_common_name_end(at())) {
            state_ = State::inside_attribute_name;
            start_ = iter_;
          } else {
            fail("Odd character “" + character() +
                 "”, expected a “>” or “/” character to end the start tag of "
                 "element “" +
                 current() +
                 "”, or optionally an attribute; perhaps you used an invalid "
                 "character in an attribute name");
          }
          if (state_ == State::after_elision_slash ||
              state_ == State::after_close_angle) {
            emit_start_element();
          }
        }
        break;
      case State::after_attribute_equals_sign:
        skip_spaces();
        if (iter_ != text_.size()) {
          if (at() != '"' && at() != '\'') {
            fail("Odd character “" + character() +
                 "”, expected an open quote mark after the equals sign when "
                 "giving value for attribute “" +
                 attributes_.back() + "” of element “" + current() + "”");
          }
          state_ = at() == '"' ? State::inside_attribute_value_dq
                               : State::inside_attribute_value_sq;
          advance();
          start_ = iter_;
        }
        break;
      case State::inside_attribute_value_sq:
      case State::inside_attribute_value_dq: {
        const char quote =
            state_ == State::inside_attribute_value_sq ? '\'' : '"';
        do {
          if (at() == quote) {
            break;
          }
        } while (advance());
        add_to_partial();
        if (iter_ != text_.size()) {
          unescape(partial_, true);
          partial_.clear();
          advance();
          state_ = State::between_attributes;
        }
        break;
      }
      case State::inside_text:
        do {
          if (at() == '<') {
            break;
          }
        } while (advance());
        add_to_partial();
        if (iter_ != text_.size()) {
          unescape(partial_, false);
          advance();
          state_ = State::after_open_angle;
          start_ = iter_;
          partial_.clear();
        }
        break;
      case State::after_close_tag_slash:
        if (is_common_name_end(at())) {
          fail("“" + character() +
               "” is not a valid character following the characters “</”; “" +
               character() + "” may not begin an element name");
        }
        state_ = State::inside_close_tag_name;
        start_ = iter_;
        break;
      case State::inside_close_tag_name:
        advance_to_name_end();
        add_to_partial();
        if (iter_ != text_.size()) {
          state_ = State::after_close_tag_name;
        }
        break;
      case State::after_close_tag_name:
        skip_spaces();
        if (iter_ != text_.size()) {
          if (at() != '>') {
            fail("“" + character() +
                 "” is not a valid character following the close element name "
                 "“" +
                 partial_ + "”; the allowed character is “>”");
          }
          if (tags_.empty()) {
            fail("Element “" + partial_ +
                 "” was closed, no element is currently open");
          }
          if (partial_ != current()) {
            fail("Element “" + partial_ +
                 "” was closed, but the currently open element is “" +
                 current() + "”");
          }
          advance();
          state_ = State::after_close_angle;
          emit_end_element();
          partial_.clear();
        }
        break;
      case State::inside_passthrough:
        passthrough();
        break;
    }
  }

  static bool ends_with(const std::string &text, const char *end) {
    const std::size_t length = std::strlen(end);
    return text.size() >= length &&
           text.compare(text.size() - length, length, end) == 0;
  }

  // The < and > are in the text of a passthrough, which ends at the > that
  // ends a processing instruction, a comment, a CDATA or a doctype.
  void passthrough() {
    do {
      if (at() == '<') {
        ++balance_;
      }
      if (at() == '>') {
        --balance_;
        add_to_partial();
        start_ = iter_;
        if (partial_[1] == '?' && partial_.back() == '?') {
          break;
        }
        if (partial_.compare(0, 4, "<!--") == 0 && ends_with(partial_, "--")) {
          break;
        }
        if (partial_.compare(0, 9, "<![CDATA[") == 0 &&
            ends_with(partial_, "]]")) {
          break;
        }
        if (partial_.compare(0, 9, "<!DOCTYPE") == 0 && balance_ == 0) {
          break;
        }
      }
    } while (advance());
    if (iter_ == text_.size()) {
      add_to_partial();
      return;
    }
    advance();
    add_to_partial();
    validate_text(partial_);
    handler_.passthrough(partial_);
    partial_.clear();
    state_ = State::after_close_angle;
    start_ = iter_;
  }

  void end_parse() const {
    if (is_document_empty_) {
      fail("Document was empty or contained only whitespace");
    }
    switch (state_) {
      case State::initial:
      case State::after_bom1:
      case State::after_bom2:
      case State::start:
        return;
      case State::after_open_angle:
        fail(
            "Document ended unexpectedly just after an open angle bracket “<”");
      case State::after_close_angle:
      case State::inside_text:
        if (!tags_.empty()) {
          fail("Document ended unexpectedly with elements still open — “" +
               current() + "” was the last element opened");
        }
        return;
      case State::after_elision_slash:
        fail(
            "Document ended unexpectedly, expected to see a close angle "
            "bracket ending the tag <" +
            current() + "/>");
      case State::inside_open_tag_name:
        fail("Document ended unexpectedly inside an element name");
      case State::inside_attribute_name:
      case State::after_attribute_name:
        fail("Document ended unexpectedly inside an attribute name");
      case State::between_attributes:
        fail("Document ended unexpectedly inside an element-opening tag.");
      case State::after_attribute_equals_sign:
        fail(
            "Document ended unexpectedly after the equals sign following an "
            "attribute name; no attribute value");
      case State::inside_attribute_value_sq:
      case State::inside_attribute_value_dq:
        fail("Document ended unexpectedly while inside an attribute value");
      case State::after_close_tag_slash:
      case State::inside_close_tag_name:
      case State::after_close_tag_name:
        if (tags_.empty()) {
          fail(
              "Document ended unexpectedly inside the close tag for an "
              "unopened element");
        }
        fail("Document ended unexpectedly inside the close tag for element “" +
             current() + "”");
      case State::inside_passthrough:
        fail(
            "Document ended unexpectedly inside a comment or processing "
            "instruction");
    }
  }
};

// What the C bosonoga checks of a document, which is one root element with at
// most one declaration before it.
class DocumentCheck : public Handler {
 public:
  bool has_declaration = false;

  void start(const std::string &element,
             const std::vector<std::string> &attributes) override {
    if (depth_ == 0 && is_closed_) {
      throw Failure("a document has one root element, but '" + element +
                    "' follows it");
    }
    for (std::size_t i = 0; i < attributes.size(); ++i) {
      for (std::size_t j = 0; j < i; ++j) {
        if (attributes[i] == attributes[j]) {
          throw Failure("'" + element + "' has the attribute '" +
                        attributes[i] + "' twice");
        }
      }
    }
    ++depth_;
  }

  void end() override {
    --depth_;
    if (depth_ == 0) {
      is_closed_ = true;
    }
  }

  void passthrough(const std::string &text) override {
    if (depth_ == 0 && !is_closed_ && !has_declaration &&
        text.compare(0, 6, "<?xml ") == 0) {
      has_declaration = true;
      return;
    }
    throw Failure(
        "an xml_str has no comment, CDATA, processing instruction or doctype, "
        "but '" +
        text.substr(0, 32) + "'");
  }

 private:
  std::size_t depth_ = 0;
  bool is_closed_ = false;
};

// GMarkup takes a < in an attribute, which XML does not.
inline bool has_quoted_open_angle(const std::string &text) {
  std::size_t i = 0;
  while (i < text.size()) {
    if (text[i] != '<') {
      ++i;
      continue;
    }
    for (++i; i < text.size() && text[i] != '>'; ++i) {
      const char quote = text[i];
      if (quote != '"' && quote != '\'') {
        continue;
      }
      for (++i; i < text.size() && text[i] != quote; ++i) {
        if (text[i] == '<') {
          return true;
        }
      }
    }
  }
  return false;
}

// Why the text is not a document, or where its root element is, after its
// declaration and without the whitespace around it.
inline std::optional<std::string> check_document(const std::string &text,
                                                 std::size_t &root,
                                                 std::size_t &root_length) {
  if (text.find('\0') != std::string::npos) {
    return "an xml_str has no NUL byte";
  }
  DocumentCheck check;
  try {
    Parser(text, check).parse();
  } catch (const Failure &failure) {
    return failure.message;
  }
  if (has_quoted_open_angle(text)) {
    return "an attribute has a <, which it writes as &lt;";
  }
  std::size_t start = check.has_declaration ? text.find("?>") + 2 : 0;
  while (start < text.size() && is_space(text[start])) {
    ++start;
  }
  std::size_t end = text.size();
  while (end > start && is_space(text[end - 1])) {
    --end;
  }
  root = start;
  root_length = end - start;
  return std::nullopt;
}

}  // namespace bosonoga::runtime::markup

#endif  // BOSONOGA_MARKUP_HH
