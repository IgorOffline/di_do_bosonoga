#include "interpreter.h"

#include "parser.h"

void daria_run(const GArray *program) {
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
  // Strings are assumed to be valid UTF-8.
  // g_print() does not report write errors.
  g_print("%s", output->str);
}
