#ifndef BOSONOGA_XML_H_
#define BOSONOGA_XML_H_

#include <glib.h>

// An xml_str is a fragment that GMarkup checks, without comments, CDATA,
// processing instructions, a doctype or a < in an attribute. A document is one
// root element, with at most one declaration before it.

#define BOSONOGA_XML_DECLARATION "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"

gboolean bosonoga_xml_is_name(const gchar *text, gsize length);

void bosonoga_xml_escape(GString *out, const gchar *text, gsize length);

gboolean bosonoga_xml_check_fragment(const gchar *text, gsize length,
                                     GError **error);

gboolean bosonoga_xml_check_document(const gchar *text, gsize length,
                                     gsize *root, gsize *root_length,
                                     GError **error);

gboolean bosonoga_xml_is_element(const gchar *text, gsize length);

gboolean bosonoga_xml_child(const gchar *text, gsize length, const gchar *name,
                            gsize name_length, guint index, gsize *child,
                            gsize *child_length);

gboolean bosonoga_xml_attribute(const gchar *text, gsize length,
                                const gchar *name, gsize name_length,
                                GString *value);

#endif  // BOSONOGA_XML_H_
