#include "interpreter.h"

#include <stdarg.h>

#include "daria.h"

#define DARIA_TOLERANCE_3 1e-3
#define DARIA_TOLERANCE_6 1e-6
// The first f32 above the i32 range, which G_MAXINT32 itself rounds up to.
#define DARIA_I32_RANGE_LIMIT 2147483648.0f

static gboolean daria_runtime_error(const DariaProgram *program,
                                    const DariaOp *op, GError **error,
                                    const gchar *format, ...)
    G_GNUC_PRINTF(4, 5);

static gboolean daria_runtime_error(const DariaProgram *program,
                                    const DariaOp *op, GError **error,
                                    const gchar *format, ...) {
  va_list args;
  va_start(args, format);
  g_autofree gchar *message = g_strdup_vprintf(format, args);
  va_end(args);
  g_set_error(error, DARIA_ERROR, DARIA_ERROR_RUNTIME,
              "%s:%" G_GSIZE_FORMAT ":%" G_GSIZE_FORMAT ": %s", program->path,
              op->line, op->column, message);
  return FALSE;
}

// Formats a real number, whose decimal separator is the letter d in any locale.
static const gchar *daria_format_real(gchar text[G_ASCII_DTOSTR_BUF_SIZE],
                                      gdouble value) {
  g_ascii_formatd(text, G_ASCII_DTOSTR_BUF_SIZE, "%.6g", value);
  return g_strdelimit(text, ".", 'd');
}

static const DariaValue *daria_operand_value(const DariaOperand *operand,
                                             const DariaValue *slots) {
  return operand->is_variable ? &slots[operand->slot] : &operand->literal;
}

// Close numbers count as equal, relative to their size or, near zero, in
// absolute terms.
static gboolean daria_f32_close(gfloat left, gfloat right, gdouble tolerance) {
  const gdouble a = (gdouble)left;
  const gdouble b = (gdouble)right;
  const gdouble difference = ABS(a - b);
  return difference <= tolerance * MAX(ABS(a), ABS(b)) ||
         difference <= tolerance;
}

// The parser has checked that the kinds fit the comparison.
static gboolean daria_compare(DariaCompareKind compare, const DariaValue *left,
                              const DariaValue *right) {
  const gboolean is_f32 = left->kind == DARIA_VALUE_F32;
  switch (compare) {
    case DARIA_COMPARE_GT:
      return is_f32 ? left->f32 > right->f32 : left->i32 > right->i32;
    case DARIA_COMPARE_LT:
      return is_f32 ? left->f32 < right->f32 : left->i32 < right->i32;
    case DARIA_COMPARE_EQ:
      return left->i32 == right->i32;
    case DARIA_COMPARE_NOT_EQ:
      return left->i32 != right->i32;
    case DARIA_COMPARE_EQ_3:
      return daria_f32_close(left->f32, right->f32, DARIA_TOLERANCE_3);
    case DARIA_COMPARE_EQ_6:
      return daria_f32_close(left->f32, right->f32, DARIA_TOLERANCE_6);
    case DARIA_COMPARE_NOT_EQ_3:
      return !daria_f32_close(left->f32, right->f32, DARIA_TOLERANCE_3);
    case DARIA_COMPARE_NOT_EQ_6:
      return !daria_f32_close(left->f32, right->f32, DARIA_TOLERANCE_6);
    case DARIA_COMPARE_SHIFT_HAS:
      return (left->flags & right->flags) == right->flags;
  }
  g_return_val_if_reached(FALSE);
}

// Intermediate results may leave the range of the variable, only the final
// result that is stored has to fit.
typedef struct {
  gint64 whole;
  gdouble real;
} DariaAccumulator;

static gboolean daria_accumulator_store(const DariaProgram *program,
                                        const DariaOp *op,
                                        const DariaAccumulator *accumulator,
                                        DariaValue *slot, GError **error) {
  if (slot->kind == DARIA_VALUE_F32) {
    if (ABS(accumulator->real) > (gdouble)G_MAXFLOAT) {
      gchar text[G_ASCII_DTOSTR_BUF_SIZE];
      return daria_runtime_error(program, op, error,
                                 "the result (%s) is outside the f32 range",
                                 daria_format_real(text, accumulator->real));
    }
    slot->f32 = (gfloat)accumulator->real;
    return TRUE;
  }
  if (accumulator->whole < G_MININT32 || accumulator->whole > G_MAXINT32) {
    return daria_runtime_error(program, op, error,
                               "the result (%" G_GINT64_FORMAT
                               ") is outside the i32 range",
                               accumulator->whole);
  }
  slot->i32 = (gint32)accumulator->whole;
  return TRUE;
}

static void daria_print_value(GString *output, const DariaValue *value) {
  switch (value->kind) {
    case DARIA_VALUE_I32:
      g_string_append_printf(output, "%" G_GINT32_FORMAT, value->i32);
      break;
    case DARIA_VALUE_F32: {
      gchar text[G_ASCII_DTOSTR_BUF_SIZE];
      g_string_append(output, daria_format_real(text, (gdouble)value->f32));
      break;
    }
    case DARIA_VALUE_FLAGS:
      g_string_append_printf(output, "%" G_GUINT32_FORMAT, value->flags);
      break;
  }
}

static gboolean daria_execute(const DariaProgram *program, DariaValue *slots,
                              GString *output, gint *exit_status,
                              GError **error) {
  DariaAccumulator accumulator = {0, 0.0};
  guint next = 0;
  while (TRUE) {
    const DariaOp *op = &g_array_index(program->ops, DariaOp, next);
    const DariaValue *left = daria_operand_value(&op->left, slots);
    const DariaValue *right = daria_operand_value(&op->right, slots);
    ++next;

    switch (op->kind) {
      case DARIA_OP_PRINT_VALUE:
        daria_print_value(output, left);
        break;
      case DARIA_OP_PRINT_STRING:
        g_string_append_len(output, op->text, (gssize)op->length);
        break;
      case DARIA_OP_PRINT_NEWLINE:
        g_string_append_c(output, '\n');
        break;
      case DARIA_OP_ASSIGN:
        slots[op->slot] = *left;
        break;
      case DARIA_OP_FLAGS_OR:
        slots[op->slot].flags |= left->flags;
        break;
      case DARIA_OP_CONVERT_TO_F32:
        slots[op->slot].kind = DARIA_VALUE_F32;
        slots[op->slot].f32 = (gfloat)left->i32;
        break;
      case DARIA_OP_CONVERT_TO_I32:
        if (!(left->f32 >= -DARIA_I32_RANGE_LIMIT &&
              left->f32 < DARIA_I32_RANGE_LIMIT)) {
          gchar text[G_ASCII_DTOSTR_BUF_SIZE];
          return daria_runtime_error(
              program, op, error, "%s is outside the i32 range",
              daria_format_real(text, (gdouble)left->f32));
        }
        slots[op->slot].kind = DARIA_VALUE_I32;
        slots[op->slot].i32 = (gint32)left->f32;
        break;
      case DARIA_OP_ACCUMULATOR_LOAD:
        if (left->kind == DARIA_VALUE_F32) {
          accumulator.real = (gdouble)left->f32;
        } else {
          accumulator.whole = left->i32;
        }
        break;
      case DARIA_OP_ACCUMULATOR_ADD:
        if (left->kind == DARIA_VALUE_F32) {
          accumulator.real += (gdouble)left->f32;
        } else {
          accumulator.whole += left->i32;
        }
        break;
      case DARIA_OP_ACCUMULATOR_SUBTRACT:
        if (left->kind == DARIA_VALUE_F32) {
          accumulator.real -= (gdouble)left->f32;
        } else {
          accumulator.whole -= left->i32;
        }
        break;
      case DARIA_OP_ACCUMULATOR_STORE:
        if (!daria_accumulator_store(program, op, &accumulator,
                                     &slots[op->slot], error)) {
          return FALSE;
        }
        break;
      case DARIA_OP_JUMP:
        next = op->target;
        break;
      case DARIA_OP_JUMP_UNLESS:
        if (!daria_compare(op->compare, left, right)) {
          next = op->target;
        }
        break;
      case DARIA_OP_LOOP_TEST:
        if (slots[op->slot].i32 >= op->integer) {
          next = op->target;
        }
        break;
      case DARIA_OP_LOOP_NEXT:
        ++slots[op->slot].i32;
        next = op->target;
        break;
      case DARIA_OP_RETURN:
        *exit_status = op->integer;
        return TRUE;
      case DARIA_OP_MISSING_RETURN:
        return daria_runtime_error(program, op, error,
                                   "the program ended without a return");
    }
  }
}

gboolean daria_run(const DariaProgram *program, GString *output,
                   gint *exit_status, GError **error) {
  g_autofree DariaValue *slots = g_new0(DariaValue, program->slot_count);
  return daria_execute(program, slots, output, exit_status, error);
}
