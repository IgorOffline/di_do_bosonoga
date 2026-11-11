#ifndef BOSONOGA_ARITHMETIC_H_
#define BOSONOGA_ARITHMETIC_H_

#include <glib.h>

#include "bosonoga/bosonoga.h"
#include "bosonoga/parser.h"
#include "value.h"

// The values of an op have the types that the parser checked.

// Only the stored result has to fit in the variable, so integers are exact in
// between, and reals are f64.
typedef struct {
  BosonogaWide whole;
  gdouble real;
} BosonogaAccumulator;

void bosonoga_print_value(GString *output, const BosonogaValue *value);

gboolean bosonoga_compare_values(BosonogaCompareKind compare,
                                 const BosonogaValue *left,
                                 const BosonogaValue *right);

void bosonoga_accumulator_load(BosonogaAccumulator *accumulator,
                               const BosonogaValue *value);

gboolean bosonoga_accumulator_step(BosonogaAccumulator *accumulator,
                                   BosonogaOpKind step,
                                   const BosonogaValue *value, GError **error);

gboolean bosonoga_accumulator_store(const BosonogaAccumulator *accumulator,
                                    BosonogaValue *slot, GError **error);

gboolean bosonoga_convert(BosonogaOpKind conversion, const BosonogaValue *value,
                          BosonogaValue *result, GError **error);

// The result has the type of left.
gboolean bosonoga_math(BosonogaOpKind function, const BosonogaValue *left,
                       const BosonogaValue *right, BosonogaValue *result,
                       GError **error);

#endif  // BOSONOGA_ARITHMETIC_H_
