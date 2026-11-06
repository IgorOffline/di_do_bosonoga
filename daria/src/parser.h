#ifndef DARIA_PARSER_H_
#define DARIA_PARSER_H_

#include <glib.h>

// The grammar of a .daria program, in which whitespace only separates tokens:
//
//   program   := "main" "di" statement* "return" "exit_success" "do"
//   statement := "info" argument+
//   argument  := INTEGER | STRING

typedef enum {
  DARIA_OP_PRINT_INTEGER,
  DARIA_OP_PRINT_STRING,
  DARIA_OP_PRINT_NEWLINE,
} DariaOpKind;

typedef struct {
  DariaOpKind kind;
  gint32 integer;
  const gchar *text;  // Slice of the source, without the quotes.
  gsize length;
} DariaOp;

// Returns the program as a new array of DariaOp, or NULL on failure. The ops
// point into source, which must outlive them. Path is only used in error
// messages, so it must be UTF-8.
GArray *daria_parse(const gchar *path, const gchar *source, gsize length,
                    GError **error);

#endif  // DARIA_PARSER_H_
