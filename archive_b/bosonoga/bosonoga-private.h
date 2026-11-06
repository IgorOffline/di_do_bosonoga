#ifndef BOSONOGA_PRIVATE_H
#define BOSONOGA_PRIVATE_H

#include "bosonoga.h"

G_BEGIN_DECLS

#define BOSONOGA_VARIABLE_NOT_FOUND BOSONOGA_VARIABLE_LIMIT
#define BOSONOGA_PARAM_LIMIT 8

#define BOSONOGA_VAR_TOKEN_COUNT 4
#define BOSONOGA_SET_MIN_TOKEN_COUNT 4
#define BOSONOGA_SHIFT_OR_TOKEN_COUNT 2
#define BOSONOGA_COMPARISON_TOKEN_COUNT 3
#define BOSONOGA_CONVERT_TOKEN_COUNT 5
#define BOSONOGA_CONVERT_SET_TOKEN_COUNT 4
#define BOSONOGA_LOOP_MIN_TOKEN_COUNT 4
#define BOSONOGA_LOOP_WITH_MIN_TOKEN_COUNT 6

#define BOSONOGA_I32_SUFFIX "_i32"
#define BOSONOGA_F32_SUFFIX "_f32"
#define BOSONOGA_LOOP_PREFIX "to_"
#define BOSONOGA_I32_RANGE_LIMIT 2147483648.0f
#define BOSONOGA_F32_TOLERANCE_3 1e-3
#define BOSONOGA_F32_TOLERANCE_6 1e-6
#define BOSONOGA_SHIFT_PREFIX "shift_"
#define BOSONOGA_SHIFT_DIGIT_LIMIT 2
#define BOSONOGA_SHIFT_BIT_LIMIT 32

#define fail_at bosonoga_fail_at

G_GNUC_INTERNAL
const gchar* bosonoga_kind_name(BosonogaKind kind);

G_GNUC_INTERNAL
gsize bosonoga_index_of_variable(const Regina* regina, const gchar* name);

G_GNUC_INTERNAL
BosonogaVariable* bosonoga_add_variable(Regina* regina, const gchar* name);

G_GNUC_INTERNAL
gboolean bosonoga_parse_i32(const gchar* text, gsize length, gint32* result);

G_GNUC_INTERNAL
gboolean bosonoga_parse_i32_literal(const gchar* token, gint32* result);

G_GNUC_INTERNAL
gboolean bosonoga_parse_f32_literal(const gchar* token, gfloat* result);

G_GNUC_INTERNAL
gboolean bosonoga_parse_flags(const Regina* regina, const gchar* token,
                              guint32* result);

G_GNUC_INTERNAL
gboolean bosonoga_parse_value(const Regina* regina, const gchar* token,
                              BosonogaValue* result);

G_GNUC_INTERNAL
gboolean bosonoga_fail_bad_value(const Regina* regina, gsize position,
                                 GError** error);

G_GNUC_INTERNAL
gboolean bosonoga_parse_value_at(const Regina* regina, gsize position,
                                 BosonogaValue* result, GError** error);

G_GNUC_INTERNAL
gboolean bosonoga_parse_flags_at(const Regina* regina, gsize position,
                                 const gchar* operation, guint32* result,
                                 GError** error);

G_GNUC_INTERNAL
gboolean bosonoga_is_conversion(const gchar* token);

G_GNUC_INTERNAL
gboolean bosonoga_convert_value(const Regina* regina, const gchar* operation,
                                gsize source_position, BosonogaValue* result,
                                GError** error);

G_GNUC_INTERNAL
gboolean bosonoga_f32_close(gfloat left, gfloat right, gdouble tolerance);

G_GNUC_INTERNAL
gboolean bosonoga_parse_return_status(const gchar* keyword, gint* status);

G_GNUC_INTERNAL
gboolean bosonoga_is_statement_keyword(const gchar* token);

G_GNUC_INTERNAL
gboolean bosonoga_run_var(Regina* regina, gsize* position, GError** error);

G_GNUC_INTERNAL
gboolean bosonoga_run_set(Regina* regina, gsize* position, GError** error);

G_END_DECLS

#endif
