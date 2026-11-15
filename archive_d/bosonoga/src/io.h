#ifndef BOSONOGA_IO_H_
#define BOSONOGA_IO_H_

#include <glib.h>

// A reason, which the caller frees, names the file as its display name.

#define BOSONOGA_IO_FILE_NAME_MAX 255

gboolean bosonoga_io_is_file_name(const gchar *text, gsize length);

gboolean bosonoga_io_read(const gchar *path, gsize max, gchar **contents,
                          gsize *length, gchar **reason);

// Only makes a new file, so taken is TRUE if it exists. A file that fails to be
// written is removed.
gboolean bosonoga_io_write(const gchar *directory, const gchar *path,
                           const gchar *text, gsize length, gboolean *taken,
                           gchar **reason);

gboolean bosonoga_io_modified(const gchar *path, gint64 *modified);

#endif  // BOSONOGA_IO_H_
