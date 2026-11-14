#ifndef BOSONOGA_TOKENIZER_H_
#define BOSONOGA_TOKENIZER_H_

#include <glib.h>

#include "bosonoga/bosonoga.h"

#define BOSONOGA_STRING_SUFFIX "_str"
#define BOSONOGA_XML_STRING_SUFFIX "_xml_str"
#define BOSONOGA_TRUE_WORD "true_bool"
#define BOSONOGA_FALSE_WORD "false_bool"

typedef enum {
  BOSONOGA_TOKEN_END,
  BOSONOGA_TOKEN_WORD,
  BOSONOGA_TOKEN_LITERAL,
  // Like 1_str, which numbers a value of a function or an array, as 0_i32 does.
  BOSONOGA_TOKEN_INDEXED_TYPE,
} BosonogaTokenKind;

typedef struct {
  BosonogaTokenKind kind;
  const gchar *path;  // Of the file of the token, for error messages.
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
