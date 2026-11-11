#ifndef BOSONOGA_VALUE_H_
#define BOSONOGA_VALUE_H_

#include <glib.h>

#include "bosonoga/bosonoga.h"

// Holds every integer type exactly, and the values in between up to 2^127.
__extension__ typedef __int128 BosonogaWide;

// Fits any BosonogaWide in decimal, with its sign and NUL.
#define BOSONOGA_WIDE_TEXT_SIZE 41

const gchar *bosonoga_value_kind_name(BosonogaValueKind kind);

gboolean bosonoga_value_kind_named(const gchar *text, gsize length,
                                   BosonogaValueKind *kind);

gboolean bosonoga_kind_is_integer(BosonogaValueKind kind);

gboolean bosonoga_kind_is_real(BosonogaValueKind kind);

gboolean bosonoga_kind_is_number(BosonogaValueKind kind);

BosonogaWide bosonoga_whole(const BosonogaValue *value);

// Of any number, rounded if it is an integer outside the precision of f64.
gdouble bosonoga_real(const BosonogaValue *value);

// FALSE, and the value is unchanged, if whole is outside the integer kind.
gboolean bosonoga_set_whole(BosonogaValue *value, BosonogaValueKind kind,
                            BosonogaWide whole);

// FALSE, and the value is unchanged, if real is NaN or outside the real kind.
gboolean bosonoga_set_real(BosonogaValue *value, BosonogaValueKind kind,
                           gdouble real);

const gchar *bosonoga_wide_text(gchar text[BOSONOGA_WIDE_TEXT_SIZE],
                                BosonogaWide whole);

#endif  // BOSONOGA_VALUE_H_
