#ifndef DARIA_TOKENIZER_H_
#define DARIA_TOKENIZER_H_

#include <glib.h>

typedef enum {
  DARIA_TOKEN_END,
  DARIA_TOKEN_WORD,
  DARIA_TOKEN_INTEGER,
  DARIA_TOKEN_UNSIGNED,
  DARIA_TOKEN_REAL,
  DARIA_TOKEN_STRING,
} DariaTokenKind;

typedef struct {
  DariaTokenKind kind;
  const gchar *text;  // Slice of the source, including the quotes of a string.
  gsize length;
  gsize line;
  gsize column;
  gint32 integer;
  guint32 unsigned_integer;
  gfloat real;
} DariaToken;

typedef struct {
  const gchar *path;
  const gchar *cursor;
  const gchar *end;
  gsize line;
  gsize column;
  DariaToken token;
} DariaTokenizer;

// The source must outlive the tokenizer and its tokens. Path is only used in
// error messages, so it must be UTF-8.
void daria_tokenizer_init(DariaTokenizer *tokenizer, const gchar *path,
                          const gchar *source, gsize length);

// Reads the next token into tokenizer->token.
gboolean daria_tokenizer_advance(DariaTokenizer *tokenizer, GError **error);

#endif  // DARIA_TOKENIZER_H_
