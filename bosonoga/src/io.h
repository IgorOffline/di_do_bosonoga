#ifndef BOSONOGA_IO_H_
#define BOSONOGA_IO_H_

#include <glib.h>

// The files of io_read, io_write and io_modified. A reason, which the caller
// frees, says what failed and names the file as its display name.

#define BOSONOGA_IO_FILE_NAME_MAX 255

// 1 to BOSONOGA_IO_FILE_NAME_MAX ASCII letters, digits, _, - and ., not first.
gboolean bosonoga_io_is_file_name(const gchar *text, gsize length);

// A regular file of at most max bytes, NUL-terminated, which the caller frees.
gboolean bosonoga_io_read(const gchar *path, gsize max, gchar **contents,
                          gsize *length, gchar **reason);

// Makes the directory if it is missing, and the file only if it is, so that
// taken is TRUE if it exists. A file that fails to be written is removed.
gboolean bosonoga_io_write(const gchar *directory, const gchar *path,
                           const gchar *text, gsize length, gboolean *taken,
                           gchar **reason);

// Of a regular file, in nanoseconds since 1970.
gboolean bosonoga_io_modified(const gchar *path, gint64 *modified);

#endif  // BOSONOGA_IO_H_
