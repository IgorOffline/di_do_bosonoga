#ifndef BOSONOGA_XML_H_
#define BOSONOGA_XML_H_

#include <glib.h>

// The XML of an xml_str, which GMarkup checks. An xml_str is a fragment:
// elements, text and the five entities or character references, without
// comments, CDATA, processing instructions or a doctype, and without an
// attribute given twice or a < in an attribute. A document is one element, the
// root, with whitespace around it and at most one declaration before it.

#define BOSONOGA_XML_DECLARATION "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"

// Of an element or attribute that a program names: an ASCII letter or _, then
// ASCII letters, digits, _, - and .
gboolean bosonoga_xml_is_name(const gchar *text, gsize length);

// Appends text with & < > " and ' as &amp; &lt; &gt; &quot; and &apos;.
void bosonoga_xml_escape(GString *out, const gchar *text, gsize length);

// The message of the error says what is wrong, but not where.
gboolean bosonoga_xml_check_fragment(const gchar *text, gsize length,
                                     GError **error);

// The message of the error says the line and character. Root is the offset of
// the root element in text.
gboolean bosonoga_xml_check_document(const gchar *text, gsize length,
                                     gsize *root, gsize *root_length,
                                     GError **error);

// Of a fragment, whether it is exactly one element.
gboolean bosonoga_xml_is_element(const gchar *text, gsize length);

// Of one element, the element of its children with the name that comes after
// index others with it, from 0.
gboolean bosonoga_xml_child(const gchar *text, gsize length, const gchar *name,
                            gsize name_length, guint index, gsize *child,
                            gsize *child_length);

// Of one element, appends the value of its attribute, with the entities and
// references replaced and tabs and line breaks as spaces.
gboolean bosonoga_xml_attribute(const gchar *text, gsize length,
                                const gchar *name, gsize name_length,
                                GString *value);

#endif  // BOSONOGA_XML_H_
