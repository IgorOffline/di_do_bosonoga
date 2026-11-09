#ifndef BOSONOGA_TOKENIZER_H_
#define BOSONOGA_TOKENIZER_H_

#include <glib.h>

typedef enum {
  BOSONOGA_TOKEN_END,
  BOSONOGA_TOKEN_WORD,
  BOSONOGA_TOKEN_INTEGER,
  BOSONOGA_TOKEN_UNSIGNED,
  BOSONOGA_TOKEN_REAL,
  BOSONOGA_TOKEN_STRING,
} BosonogaTokenKind;

typedef struct {
  BosonogaTokenKind kind;
  const gchar *text;  // Slice of the source, including the quotes of a string.
  gsize length;
  gsize line;
  gsize column;
  gint32 integer;
  guint32 unsigned_integer;
  gfloat real;
} BosonogaToken;

typedef struct {
  const gchar *path;
  const gchar *cursor;
  const gchar *end;
  gsize line;
  gsize column;
  BosonogaToken token;
} BosonogaTokenizer;

// The source must outlive the tokenizer and its tokens. Path is only used in
// error messages, so it must be UTF-8.
void bosonoga_tokenizer_init(BosonogaTokenizer *tokenizer, const gchar *path,
                             const gchar *source, gsize length);

// Reads the next token into tokenizer->token.
gboolean bosonoga_tokenizer_advance(BosonogaTokenizer *tokenizer,
                                    GError **error);

#endif  // BOSONOGA_TOKENIZER_H_
