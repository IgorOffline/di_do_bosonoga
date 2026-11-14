// For st_mtim and O_CLOEXEC, which C11 alone does not have.
#define _POSIX_C_SOURCE 200809L

#include "io.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

gboolean bosonoga_io_is_file_name(const gchar *text, gsize length) {
  if (length == 0 || length > BOSONOGA_IO_FILE_NAME_MAX || text[0] == '.') {
    return FALSE;
  }
  for (gsize i = 0; i < length; ++i) {
    if (!g_ascii_isalnum(text[i]) && text[i] != '_' && text[i] != '-' &&
        text[i] != '.') {
      return FALSE;
    }
  }
  return TRUE;
}

static gchar *bosonoga_io_reason(const gchar *path, const gchar *what) {
  g_autofree gchar *shown = g_filename_display_name(path);
  return g_strdup_printf("%s: %s", shown, what);
}

gboolean bosonoga_io_read(const gchar *path, gsize max, gchar **contents,
                          gsize *length, gchar **reason) {
  const gint fd = g_open(path, O_RDONLY | O_CLOEXEC, 0);
  if (fd < 0) {
    *reason = bosonoga_io_reason(path, g_strerror(errno));
    return FALSE;
  }
  struct stat status;
  if (fstat(fd, &status) != 0) {
    *reason = bosonoga_io_reason(path, g_strerror(errno));
    g_close(fd, NULL);
    return FALSE;
  }
  if (!S_ISREG(status.st_mode)) {
    *reason = bosonoga_io_reason(path, "is not a file");
    g_close(fd, NULL);
    return FALSE;
  }
  g_autoptr(GString) text = g_string_new(NULL);
  gchar buffer[65536];
  while (TRUE) {
    const gssize count = read(fd, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      *reason = bosonoga_io_reason(path, g_strerror(errno));
      g_close(fd, NULL);
      return FALSE;
    }
    if (count == 0) {
      break;
    }
    g_string_append_len(text, buffer, count);
    if (text->len > max) {
      g_autofree gchar *what =
          g_strdup_printf("is longer than %" G_GSIZE_FORMAT " bytes", max);
      *reason = bosonoga_io_reason(path, what);
      g_close(fd, NULL);
      return FALSE;
    }
  }
  g_close(fd, NULL);
  *length = text->len;
  *contents = g_string_free_and_steal(g_steal_pointer(&text));
  return TRUE;
}

gboolean bosonoga_io_write(const gchar *directory, const gchar *path,
                           const gchar *text, gsize length, gboolean *taken,
                           gchar **reason) {
  *taken = FALSE;
  if (g_mkdir_with_parents(directory, 0755) != 0) {
    *reason = bosonoga_io_reason(directory, g_strerror(errno));
    return FALSE;
  }
  const gint fd = g_open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (fd < 0) {
    *taken = errno == EEXIST;
    *reason = bosonoga_io_reason(path, g_strerror(errno));
    return FALSE;
  }
  gsize written = 0;
  while (written < length) {
    const gssize count = write(fd, text + written, length - written);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      *reason = bosonoga_io_reason(path, g_strerror(errno));
      g_close(fd, NULL);
      g_unlink(path);
      return FALSE;
    }
    written += (gsize)count;
  }
  GError *close_error = NULL;
  if (!g_close(fd, &close_error)) {
    *reason = bosonoga_io_reason(path, close_error->message);
    g_error_free(close_error);
    g_unlink(path);
    return FALSE;
  }
  return TRUE;
}

gboolean bosonoga_io_modified(const gchar *path, gint64 *modified) {
  struct stat status;
  if (stat(path, &status) != 0 || !S_ISREG(status.st_mode)) {
    return FALSE;
  }
  *modified = (gint64)status.st_mtim.tv_sec * G_GINT64_CONSTANT(1000000000) +
              (gint64)status.st_mtim.tv_nsec;
  return TRUE;
}
