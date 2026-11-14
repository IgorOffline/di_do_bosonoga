#include "xml.h"

#include <string.h>

#include "bosonoga/bosonoga.h"

// A fragment is checked inside an element of this name, so that it may have
// text and many elements at its top.
#define BOSONOGA_XML_WRAPPER "bosonoga_fragment"
#define BOSONOGA_XML_MESSAGE_PREFIX "Error on line"

typedef struct {
  GPtrArray *open;  // The names of the elements that are open, innermost last.
  guint depth;
  gboolean is_closed;  // The outermost element ended.
  gboolean is_document;
  gboolean has_declaration;
} BosonogaXmlCheck;

gboolean bosonoga_xml_is_name(const gchar *text, gsize length) {
  if (length == 0 || !(g_ascii_isalpha(text[0]) || text[0] == '_')) {
    return FALSE;
  }
  for (gsize i = 1; i < length; ++i) {
    if (!g_ascii_isalnum(text[i]) && text[i] != '_' && text[i] != '-' &&
        text[i] != '.') {
      return FALSE;
    }
  }
  return TRUE;
}

void bosonoga_xml_escape(GString *out, const gchar *text, gsize length) {
  for (gsize i = 0; i < length; ++i) {
    switch (text[i]) {
      case '&':
        g_string_append(out, "&amp;");
        break;
      case '<':
        g_string_append(out, "&lt;");
        break;
      case '>':
        g_string_append(out, "&gt;");
        break;
      case '"':
        g_string_append(out, "&quot;");
        break;
      case '\'':
        g_string_append(out, "&apos;");
        break;
      default:
        g_string_append_c(out, text[i]);
        break;
    }
  }
}

static void bosonoga_xml_start(GMarkupParseContext *context,
                               const gchar *element_name,
                               const gchar **attribute_names,
                               const gchar **attribute_values,
                               gpointer user_data, GError **error) {
  (void)context;
  (void)attribute_values;
  BosonogaXmlCheck *check = user_data;
  if (check->depth == 0 && check->is_closed) {
    g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                check->is_document
                    ? "a document has one root element, but '%s' follows it"
                    : "an element is closed that was not opened before '%s'",
                element_name);
    return;
  }
  for (gsize i = 0; attribute_names[i] != NULL; ++i) {
    for (gsize j = 0; j < i; ++j) {
      if (g_str_equal(attribute_names[i], attribute_names[j])) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "'%s' has the attribute '%s' twice", element_name,
                    attribute_names[i]);
        return;
      }
    }
  }
  ++check->depth;
  g_ptr_array_add(check->open, g_strdup(element_name));
}

static void bosonoga_xml_end(GMarkupParseContext *context,
                             const gchar *element_name, gpointer user_data,
                             GError **error) {
  (void)context;
  (void)element_name;
  (void)error;
  BosonogaXmlCheck *check = user_data;
  --check->depth;
  g_ptr_array_set_size(check->open, (gint)check->depth);
  if (check->depth == 0) {
    check->is_closed = TRUE;
  }
}

static void bosonoga_xml_passthrough(GMarkupParseContext *context,
                                     const gchar *text, gsize length,
                                     gpointer user_data, GError **error) {
  (void)context;
  BosonogaXmlCheck *check = user_data;
  static const gchar declaration[] = "<?xml ";
  if (check->is_document && check->depth == 0 && !check->is_closed &&
      !check->has_declaration && length >= strlen(declaration) &&
      memcmp(text, declaration, strlen(declaration)) == 0) {
    check->has_declaration = TRUE;
    return;
  }
  g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
              "an xml_str has no comment, CDATA, processing instruction or "
              "doctype, but '%.*s'",
              (gint)MIN(length, 32), text);
}

// The offset after the > of the tag that starts at start, of which GMarkup
// checked that its quotes close.
static gsize bosonoga_xml_tag_end(const gchar *text, gsize length, gsize start,
                                  gboolean *is_empty) {
  for (gsize i = start + 1; i < length; ++i) {
    if (text[i] == '"' || text[i] == '\'') {
      const gchar quote = text[i];
      ++i;
      while (i < length && text[i] != quote) {
        ++i;
      }
    } else if (text[i] == '>') {
      *is_empty = text[i - 1] == '/';
      return i + 1;
    }
  }
  return length;
}

// GMarkup takes < in an attribute, which XML does not.
static gboolean bosonoga_xml_check_quotes(const gchar *text, gsize length,
                                          GError **error) {
  gsize i = 0;
  while (i < length) {
    if (text[i] != '<') {
      ++i;
      continue;
    }
    for (++i; i < length && text[i] != '>'; ++i) {
      if (text[i] != '"' && text[i] != '\'') {
        continue;
      }
      const gchar quote = text[i];
      for (++i; i < length && text[i] != quote; ++i) {
        if (text[i] == '<') {
          g_set_error_literal(error, G_MARKUP_ERROR,
                              G_MARKUP_ERROR_INVALID_CONTENT,
                              "an attribute has a <, which it writes as &lt;");
          return FALSE;
        }
      }
    }
  }
  return TRUE;
}

static gboolean bosonoga_xml_parse(const gchar *text, gsize length,
                                   BosonogaXmlCheck *check, GError **error) {
  if (memchr(text, '\0', length) != NULL) {
    g_set_error_literal(error, G_MARKUP_ERROR, G_MARKUP_ERROR_BAD_UTF8,
                        "an xml_str has no NUL byte");
    return FALSE;
  }
  static const GMarkupParser parser = {
      bosonoga_xml_start,
      bosonoga_xml_end,
      NULL,
      bosonoga_xml_passthrough,
      NULL,
  };
  GMarkupParseContext *context =
      g_markup_parse_context_new(&parser, (GMarkupParseFlags)0, check, NULL);
  const gboolean parsed =
      g_markup_parse_context_parse(context, text, (gssize)length, error) &&
      g_markup_parse_context_end_parse(context, error);
  g_markup_parse_context_free(context);
  return parsed && bosonoga_xml_check_quotes(text, length, error);
}

gboolean bosonoga_xml_check_fragment(const gchar *text, gsize length,
                                     GError **error) {
  g_autoptr(GString) wrapped = g_string_new("<" BOSONOGA_XML_WRAPPER ">");
  g_string_append_len(wrapped, text, (gssize)length);
  g_string_append(wrapped, "</" BOSONOGA_XML_WRAPPER ">");
  g_autoptr(GPtrArray) open = g_ptr_array_new_with_free_func(g_free);
  BosonogaXmlCheck check = {.open = open};
  GError *parse_error = NULL;
  if (bosonoga_xml_parse(wrapped->str, wrapped->len, &check, &parse_error)) {
    return TRUE;
  }
  // GMarkup says where in the wrapped text, which is not where in the text,
  // and may name the element around it.
  const gchar *message = parse_error->message;
  if (g_str_has_prefix(message, BOSONOGA_XML_MESSAGE_PREFIX) &&
      strstr(message, ": ") != NULL) {
    message = strstr(message, ": ") + 2;
  }
  if (strstr(message, BOSONOGA_XML_WRAPPER) != NULL && open->len > 1) {
    g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_SYNTAX,
                "'%s' is not closed",
                (const gchar *)g_ptr_array_index(open, open->len - 1));
  } else if (strstr(message, BOSONOGA_XML_WRAPPER) != NULL) {
    g_set_error_literal(error, BOSONOGA_ERROR, BOSONOGA_ERROR_SYNTAX,
                        "an element is closed that was not opened");
  } else {
    g_set_error_literal(error, BOSONOGA_ERROR, BOSONOGA_ERROR_SYNTAX, message);
  }
  g_error_free(parse_error);
  return FALSE;
}

static gboolean bosonoga_xml_is_blank(gchar c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

gboolean bosonoga_xml_check_document(const gchar *text, gsize length,
                                     gsize *root, gsize *root_length,
                                     GError **error) {
  g_autoptr(GPtrArray) open = g_ptr_array_new_with_free_func(g_free);
  BosonogaXmlCheck check = {.open = open, .is_document = TRUE};
  if (!bosonoga_xml_parse(text, length, &check, error)) {
    return FALSE;
  }
  // GMarkup checked the declaration and that whitespace is around the root.
  gsize start = 0;
  if (check.has_declaration) {
    start = (gsize)(g_strstr_len(text, (gssize)length, "?>") - text) + 2;
  }
  while (bosonoga_xml_is_blank(text[start])) {
    ++start;
  }
  gsize end = length;
  while (bosonoga_xml_is_blank(text[end - 1])) {
    --end;
  }
  *root = start;
  *root_length = end - start;
  return TRUE;
}

// The offset after the element that starts at start.
static gsize bosonoga_xml_element_end(const gchar *text, gsize length,
                                      gsize start) {
  gboolean is_empty = FALSE;
  gsize i = bosonoga_xml_tag_end(text, length, start, &is_empty);
  if (is_empty) {
    return i;
  }
  guint depth = 1;
  while (i < length) {
    const gchar *found = memchr(text + i, '<', length - i);
    if (found == NULL) {
      return length;
    }
    const gsize tag = (gsize)(found - text);
    i = bosonoga_xml_tag_end(text, length, tag, &is_empty);
    if (text[tag + 1] == '/') {
      if (--depth == 0) {
        return i;
      }
    } else if (!is_empty) {
      ++depth;
    }
  }
  return length;
}

gboolean bosonoga_xml_is_element(const gchar *text, gsize length) {
  return length > 0 && text[0] == '<' &&
         bosonoga_xml_element_end(text, length, 0) == length;
}

static gsize bosonoga_xml_name_length(const gchar *text, gsize length,
                                      gsize start) {
  gsize end = start;
  while (end < length && !bosonoga_xml_is_blank(text[end]) &&
         text[end] != '/' && text[end] != '>' && text[end] != '=') {
    ++end;
  }
  return end - start;
}

gboolean bosonoga_xml_child(const gchar *text, gsize length, const gchar *name,
                            gsize name_length, guint index, gsize *child,
                            gsize *child_length) {
  gboolean is_empty = FALSE;
  gsize i = bosonoga_xml_tag_end(text, length, 0, &is_empty);
  if (is_empty) {
    return FALSE;
  }
  guint seen = 0;
  while (i < length) {
    const gchar *found = memchr(text + i, '<', length - i);
    if (found == NULL || found[1] == '/') {
      return FALSE;
    }
    const gsize start = (gsize)(found - text);
    const gsize end = bosonoga_xml_element_end(text, length, start);
    if (bosonoga_xml_name_length(text, length, start + 1) == name_length &&
        memcmp(text + start + 1, name, name_length) == 0) {
      if (seen == index) {
        *child = start;
        *child_length = end - start;
        return TRUE;
      }
      ++seen;
    }
    i = end;
  }
  return FALSE;
}

// Of an entity or reference that GMarkup checked, which starts after its &
// and ends with a ; before text + available.
static gsize bosonoga_xml_unescape_one(const gchar *text, gsize available,
                                       GString *value) {
  const gchar *end = memchr(text, ';', available);
  const gsize length = (gsize)(end - text);
  static const struct {
    const gchar *name;
    gchar character;
  } entities[] = {
      {"lt", '<'}, {"gt", '>'}, {"amp", '&'}, {"quot", '"'}, {"apos", '\''},
  };
  for (gsize i = 0; i < G_N_ELEMENTS(entities); ++i) {
    if (strlen(entities[i].name) == length &&
        memcmp(text, entities[i].name, length) == 0) {
      g_string_append_c(value, entities[i].character);
      return length + 1;
    }
  }
  const gboolean is_hex = text[1] == 'x';
  g_autofree gchar *digits =
      g_strndup(text + (is_hex ? 2 : 1), length - (is_hex ? 2 : 1));
  g_string_append_unichar(
      value, (gunichar)g_ascii_strtoull(digits, NULL, is_hex ? 16 : 10));
  return length + 1;
}

gboolean bosonoga_xml_attribute(const gchar *text, gsize length,
                                const gchar *name, gsize name_length,
                                GString *value) {
  gboolean is_empty = FALSE;
  const gsize tag_end = bosonoga_xml_tag_end(text, length, 0, &is_empty);
  gsize i = 1 + bosonoga_xml_name_length(text, length, 1);
  while (i < tag_end) {
    while (bosonoga_xml_is_blank(text[i])) {
      ++i;
    }
    if (text[i] == '/' || text[i] == '>') {
      return FALSE;
    }
    const gsize attribute = i;
    const gsize attribute_length = bosonoga_xml_name_length(text, length, i);
    i += attribute_length;
    while (text[i] != '"' && text[i] != '\'') {
      ++i;
    }
    const gchar quote = text[i++];
    const gsize value_start = i;
    while (text[i] != quote) {
      ++i;
    }
    const gsize value_end = i++;
    if (attribute_length != name_length ||
        memcmp(text + attribute, name, name_length) != 0) {
      continue;
    }
    for (gsize j = value_start; j < value_end; ++j) {
      if (text[j] == '&') {
        j += bosonoga_xml_unescape_one(text + j + 1, value_end - j - 1, value);
      } else if (text[j] == '\t' || text[j] == '\n' || text[j] == '\r') {
        g_string_append_c(value, ' ');
      } else {
        g_string_append_c(value, text[j]);
      }
    }
    return TRUE;
  }
  return FALSE;
}
