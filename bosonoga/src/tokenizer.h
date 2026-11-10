#ifndef BOSONOGA_TOKENIZER_H_
#define BOSONOGA_TOKENIZER_H_

#include <glib.h>

#include "bosonoga/bosonoga.h"

#define BOSONOGA_STRING_SUFFIX "_str"

typedef enum {
  BOSONOGA_TOKEN_END,
  BOSONOGA_TOKEN_WORD,
  BOSONOGA_TOKEN_LITERAL,  // A number or a string.
  // Digits and _flags, _color or _str, like 1_str, which only numbers a value
  // in the types of a function or an array, as the
  // literals 0_i32 and 0_f32 do.
  BOSONOGA_TOKEN_INDEXED_TYPE,
} BosonogaTokenKind;

typedef struct {
  BosonogaTokenKind kind;
  const gchar *text;  // Slice of the source, the whole token.
  gsize length;
  gsize line;
  gsize column;
  BosonogaValue value;  // Of a literal. A str points between the quotes.
} BosonogaToken;

typedef struct {
  const gchar *path;
  const gchar *cursor;
  const gchar *end;
  gsize line;
  gsize column;
  BosonogaToken token;
} BosonogaTokenizer;

// The source must outlive the tokens. Path is UTF-8, for error messages.
void bosonoga_tokenizer_init(BosonogaTokenizer *tokenizer, const gchar *path,
                             const gchar *source, gsize length);

gboolean bosonoga_tokenizer_advance(BosonogaTokenizer *tokenizer,
                                    GError **error);

#endif  // BOSONOGA_TOKENIZER_H_
