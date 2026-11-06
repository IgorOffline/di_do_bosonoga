#include "interpreter.h"

#include "daria.h"
#include "parser.h"

#define DARIA_STDOUT_FILENO 1

static gboolean daria_write_stdout(const GString *output, GError **error) {
  g_autoptr(GIOChannel) channel = g_io_channel_unix_new(DARIA_STDOUT_FILENO);
  if ((g_io_channel_get_flags(channel) & G_IO_FLAG_IS_WRITABLE) == 0) {
    g_set_error_literal(error, DARIA_ERROR, DARIA_ERROR_OUTPUT,
                        "stdout is not writable");
    return FALSE;
  }

  // Binary, because g_print() would convert the bytes of a string to the
  // console charset and rewrite any that are not valid UTF-8. Unbuffered,
  // because after a failed write, unreferencing a buffered channel flushes it
  // again and logs a warning.
  if (g_io_channel_set_encoding(channel, NULL, error) != G_IO_STATUS_NORMAL) {
    return FALSE;
  }
  g_io_channel_set_buffered(channel, FALSE);

  gsize offset = 0;
  while (offset < output->len) {
    gsize written = 0;
    // A non-blocking stdout reports G_IO_STATUS_AGAIN, which is retried.
    if (g_io_channel_write_chars(channel, output->str + offset,
                                 (gssize)(output->len - offset), &written,
                                 error) == G_IO_STATUS_ERROR) {
      return FALSE;
    }
    offset += written;
  }
  return TRUE;
}

gboolean daria_run(const GArray *program, GError **error) {
  g_autoptr(GString) output = g_string_new(NULL);
  for (guint i = 0; i < program->len; ++i) {
    const DariaOp *op = &g_array_index(program, DariaOp, i);
    switch (op->kind) {
      case DARIA_OP_PRINT_INTEGER:
        g_string_append_printf(output, "%" G_GINT32_FORMAT, op->integer);
        break;
      case DARIA_OP_PRINT_STRING:
        g_string_append_len(output, op->text, (gssize)op->length);
        break;
      case DARIA_OP_PRINT_NEWLINE:
        g_string_append_c(output, '\n');
        break;
    }
  }
  return daria_write_stdout(output, error);
}
