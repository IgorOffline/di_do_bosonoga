#include "value.h"

#include <math.h>
#include <string.h>

static const BosonogaValueKind bosonoga_kinds[] = {
    BOSONOGA_VALUE_I32,   BOSONOGA_VALUE_I64,   BOSONOGA_VALUE_U32,
    BOSONOGA_VALUE_U64,   BOSONOGA_VALUE_F32,   BOSONOGA_VALUE_F64,
    BOSONOGA_VALUE_FLAGS, BOSONOGA_VALUE_COLOR, BOSONOGA_VALUE_STR,
    BOSONOGA_VALUE_BOOL,
};

const gchar *bosonoga_value_kind_name(BosonogaValueKind kind) {
  switch (kind) {
    case BOSONOGA_VALUE_I32:
      return "i32";
    case BOSONOGA_VALUE_I64:
      return "i64";
    case BOSONOGA_VALUE_U32:
      return "u32";
    case BOSONOGA_VALUE_U64:
      return "u64";
    case BOSONOGA_VALUE_F32:
      return "f32";
    case BOSONOGA_VALUE_F64:
      return "f64";
    case BOSONOGA_VALUE_FLAGS:
      return "flags";
    case BOSONOGA_VALUE_COLOR:
      return "color";
    case BOSONOGA_VALUE_STR:
      return "str";
    case BOSONOGA_VALUE_BOOL:
      return "bool";
  }
  g_return_val_if_reached("unknown");
}

gboolean bosonoga_value_kind_named(const gchar *text, gsize length,
                                   BosonogaValueKind *kind) {
  for (gsize i = 0; i < G_N_ELEMENTS(bosonoga_kinds); ++i) {
    const gchar *name = bosonoga_value_kind_name(bosonoga_kinds[i]);
    if (strlen(name) == length && memcmp(name, text, length) == 0) {
      *kind = bosonoga_kinds[i];
      return TRUE;
    }
  }
  return FALSE;
}

gboolean bosonoga_kind_is_integer(BosonogaValueKind kind) {
  return kind == BOSONOGA_VALUE_I32 || kind == BOSONOGA_VALUE_I64 ||
         kind == BOSONOGA_VALUE_U32 || kind == BOSONOGA_VALUE_U64;
}

gboolean bosonoga_kind_is_real(BosonogaValueKind kind) {
  return kind == BOSONOGA_VALUE_F32 || kind == BOSONOGA_VALUE_F64;
}

gboolean bosonoga_kind_is_number(BosonogaValueKind kind) {
  return bosonoga_kind_is_integer(kind) || bosonoga_kind_is_real(kind);
}

BosonogaWide bosonoga_whole(const BosonogaValue *value) {
  switch (value->kind) {
    case BOSONOGA_VALUE_I32:
      return value->i32;
    case BOSONOGA_VALUE_I64:
      return value->i64;
    case BOSONOGA_VALUE_U32:
      return value->u32;
    case BOSONOGA_VALUE_U64:
      return value->u64;
    case BOSONOGA_VALUE_F32:
    case BOSONOGA_VALUE_F64:
    case BOSONOGA_VALUE_FLAGS:
    case BOSONOGA_VALUE_COLOR:
    case BOSONOGA_VALUE_STR:
    case BOSONOGA_VALUE_BOOL:
      break;
  }
  g_return_val_if_reached(0);
}

gdouble bosonoga_real(const BosonogaValue *value) {
  if (value->kind == BOSONOGA_VALUE_F64) {
    return value->f64;
  }
  if (value->kind == BOSONOGA_VALUE_F32) {
    return (gdouble)value->f32;
  }
  return (gdouble)bosonoga_whole(value);
}

gboolean bosonoga_set_whole(BosonogaValue *value, BosonogaValueKind kind,
                            BosonogaWide whole) {
  switch (kind) {
    case BOSONOGA_VALUE_I32:
      if (whole < G_MININT32 || whole > G_MAXINT32) {
        return FALSE;
      }
      value->i32 = (gint32)whole;
      break;
    case BOSONOGA_VALUE_I64:
      if (whole < G_MININT64 || whole > G_MAXINT64) {
        return FALSE;
      }
      value->i64 = (gint64)whole;
      break;
    case BOSONOGA_VALUE_U32:
      if (whole < 0 || whole > G_MAXUINT32) {
        return FALSE;
      }
      value->u32 = (guint32)whole;
      break;
    case BOSONOGA_VALUE_U64:
      if (whole < 0 || whole > G_MAXUINT64) {
        return FALSE;
      }
      value->u64 = (guint64)whole;
      break;
    case BOSONOGA_VALUE_F32:
    case BOSONOGA_VALUE_F64:
    case BOSONOGA_VALUE_FLAGS:
    case BOSONOGA_VALUE_COLOR:
    case BOSONOGA_VALUE_STR:
    case BOSONOGA_VALUE_BOOL:
      g_return_val_if_reached(FALSE);
  }
  value->kind = kind;
  return TRUE;
}

gboolean bosonoga_set_real(BosonogaValue *value, BosonogaValueKind kind,
                           gdouble real) {
  if (kind == BOSONOGA_VALUE_F64) {
    if (!isfinite(real)) {
      return FALSE;
    }
    value->f64 = real;
  } else {
    g_return_val_if_fail(kind == BOSONOGA_VALUE_F32, FALSE);
    // Written so that NaN is outside the range too.
    if (!(ABS(real) <= (gdouble)G_MAXFLOAT)) {
      return FALSE;
    }
    value->f32 = (gfloat)real;
  }
  value->kind = kind;
  return TRUE;
}

// No printf format takes a BosonogaWide.
const gchar *bosonoga_wide_text(gchar text[BOSONOGA_WIDE_TEXT_SIZE],
                                BosonogaWide whole) {
  const gboolean negative = whole < 0;
  gchar *cursor = text + BOSONOGA_WIDE_TEXT_SIZE - 1;
  *cursor = '\0';
  // The digits are taken from the remainders, so that the most negative
  // value is never negated.
  do {
    const BosonogaWide digit = whole % 10;
    *--cursor = (gchar)('0' + (digit < 0 ? -digit : digit));
    whole /= 10;
  } while (whole != 0);
  if (negative) {
    *--cursor = '-';
  }
  return cursor;
}
