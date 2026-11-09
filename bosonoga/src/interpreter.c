#include "bosonoga/interpreter.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>

#include "bosonoga/bosonoga.h"

#define BOSONOGA_TOLERANCE_3 1e-3
#define BOSONOGA_TOLERANCE_6 1e-6
// The first f32 above the i32 range, which G_MAXINT32 itself rounds up to.
#define BOSONOGA_I32_RANGE_LIMIT 2147483648.0f

static gboolean bosonoga_runtime_error(const BosonogaProgram *program,
                                       const BosonogaOp *op, GError **error,
                                       const gchar *format, ...)
    G_GNUC_PRINTF(4, 5);

static gboolean bosonoga_runtime_error(const BosonogaProgram *program,
                                       const BosonogaOp *op, GError **error,
                                       const gchar *format, ...) {
  va_list args;
  va_start(args, format);
  g_autofree gchar *message = g_strdup_vprintf(format, args);
  va_end(args);
  g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
              "%s:%" G_GSIZE_FORMAT ":%" G_GSIZE_FORMAT ": %s", program->path,
              op->line, op->column, message);
  return FALSE;
}

static const gchar *bosonoga_format_real(gchar text[G_ASCII_DTOSTR_BUF_SIZE],
                                         gdouble value) {
  g_ascii_formatd(text, G_ASCII_DTOSTR_BUF_SIZE, "%.6g", value);
  return g_strdelimit(text, ".", 'd');
}

static const BosonogaValue *bosonoga_operand_value(
    const BosonogaOperand *operand, const BosonogaValue *slots) {
  return operand->is_variable ? &slots[operand->slot] : &operand->literal;
}

static gboolean bosonoga_f32_close(gfloat left, gfloat right,
                                   gdouble tolerance) {
  const gdouble a = (gdouble)left;
  const gdouble b = (gdouble)right;
  const gdouble difference = ABS(a - b);
  return difference <= tolerance * MAX(ABS(a), ABS(b)) ||
         difference <= tolerance;
}

static gboolean bosonoga_str_equal(const BosonogaValue *left,
                                   const BosonogaValue *right) {
  return left->str.length == right->str.length &&
         memcmp(left->str.text, right->str.text, left->str.length) == 0;
}

static gboolean bosonoga_compare(BosonogaCompareKind compare,
                                 const BosonogaValue *left,
                                 const BosonogaValue *right) {
  const gboolean is_f32 = left->kind == BOSONOGA_VALUE_F32;
  const gboolean is_str = left->kind == BOSONOGA_VALUE_STR;
  switch (compare) {
    case BOSONOGA_COMPARE_GT:
      return is_f32 ? left->f32 > right->f32 : left->i32 > right->i32;
    case BOSONOGA_COMPARE_LT:
      return is_f32 ? left->f32 < right->f32 : left->i32 < right->i32;
    case BOSONOGA_COMPARE_GE:
      return is_f32 ? left->f32 >= right->f32 : left->i32 >= right->i32;
    case BOSONOGA_COMPARE_LE:
      return is_f32 ? left->f32 <= right->f32 : left->i32 <= right->i32;
    case BOSONOGA_COMPARE_EQ:
      return is_str ? bosonoga_str_equal(left, right) : left->i32 == right->i32;
    case BOSONOGA_COMPARE_NOT_EQ:
      return is_str ? !bosonoga_str_equal(left, right)
                    : left->i32 != right->i32;
    case BOSONOGA_COMPARE_EQ_3:
      return bosonoga_f32_close(left->f32, right->f32, BOSONOGA_TOLERANCE_3);
    case BOSONOGA_COMPARE_EQ_6:
      return bosonoga_f32_close(left->f32, right->f32, BOSONOGA_TOLERANCE_6);
    case BOSONOGA_COMPARE_NOT_EQ_3:
      return !bosonoga_f32_close(left->f32, right->f32, BOSONOGA_TOLERANCE_3);
    case BOSONOGA_COMPARE_NOT_EQ_6:
      return !bosonoga_f32_close(left->f32, right->f32, BOSONOGA_TOLERANCE_6);
    case BOSONOGA_COMPARE_SHIFT_HAS:
      return (left->flags & right->flags) == right->flags;
  }
  g_return_val_if_reached(FALSE);
}

// Only the stored result has to fit in the variable.
typedef struct {
  gint64 whole;
  gdouble real;
} BosonogaAccumulator;

struct _BosonogaExecution {
  const BosonogaProgram *program;
  BosonogaValue *slots;
  BosonogaAccumulator accumulator;
  guint next;
  gint64 wake_time;
  BosonogaGfxScene *scene;
  GString *output;
  gboolean to_stdout;  // Then output is an owned buffer for stdout.
};

static gboolean bosonoga_accumulator_store(
    const BosonogaProgram *program, const BosonogaOp *op,
    const BosonogaAccumulator *accumulator, BosonogaValue *slot,
    GError **error) {
  if (slot->kind == BOSONOGA_VALUE_F32) {
    // Written so that NaN, from infinity times 0, is outside the range too.
    if (!(ABS(accumulator->real) <= (gdouble)G_MAXFLOAT)) {
      gchar text[G_ASCII_DTOSTR_BUF_SIZE];
      return bosonoga_runtime_error(
          program, op, error, "the result (%s) is outside the f32 range",
          bosonoga_format_real(text, accumulator->real));
    }
    slot->f32 = (gfloat)accumulator->real;
    return TRUE;
  }
  if (accumulator->whole < G_MININT32 || accumulator->whole > G_MAXINT32) {
    return bosonoga_runtime_error(program, op, error,
                                  "the result (%" G_GINT64_FORMAT
                                  ") is outside the i32 range",
                                  accumulator->whole);
  }
  slot->i32 = (gint32)accumulator->whole;
  return TRUE;
}

// The accumulator is 64-bit, so the smallest i32 divided by -1 fits.
static gboolean bosonoga_accumulator_divide(const BosonogaProgram *program,
                                            const BosonogaOp *op,
                                            BosonogaAccumulator *accumulator,
                                            const BosonogaValue *left,
                                            GError **error) {
  const gboolean is_mod = op->kind == BOSONOGA_OP_ACCUMULATOR_MOD;
  if (left->kind == BOSONOGA_VALUE_F32) {
    if (!(left->f32 < 0.0f || left->f32 > 0.0f)) {
      return bosonoga_runtime_error(program, op, error, "division by zero");
    }
    accumulator->real = is_mod ? fmod(accumulator->real, (gdouble)left->f32)
                               : accumulator->real / (gdouble)left->f32;
    return TRUE;
  }
  if (left->i32 == 0) {
    return bosonoga_runtime_error(program, op, error, "division by zero");
  }
  accumulator->whole =
      is_mod ? accumulator->whole % left->i32 : accumulator->whole / left->i32;
  return TRUE;
}

static void bosonoga_print_value(GString *output, const BosonogaValue *value) {
  switch (value->kind) {
    case BOSONOGA_VALUE_I32:
      g_string_append_printf(output, "%" G_GINT32_FORMAT, value->i32);
      break;
    case BOSONOGA_VALUE_F32: {
      gchar text[G_ASCII_DTOSTR_BUF_SIZE];
      g_string_append(output, bosonoga_format_real(text, (gdouble)value->f32));
      break;
    }
    case BOSONOGA_VALUE_FLAGS:
      g_string_append_printf(output, "%" G_GUINT32_FORMAT, value->flags);
      break;
    case BOSONOGA_VALUE_COLOR:
      g_string_append_printf(output, "%06" G_GINT32_MODIFIER "X", value->color);
      break;
    case BOSONOGA_VALUE_STR:
      g_string_append_len(output, value->str.text, (gssize)value->str.length);
      break;
  }
}

static BosonogaExecutionState bosonoga_execute(BosonogaExecution *execution,
                                               guint op_budget,
                                               gint *exit_status,
                                               GError **error) {
  const BosonogaProgram *program = execution->program;
  BosonogaValue *slots = execution->slots;
  GString *output = execution->output;
  BosonogaGfxScene *scene = execution->scene;
  BosonogaAccumulator *accumulator = &execution->accumulator;

  if (g_get_monotonic_time() < execution->wake_time) {
    return BOSONOGA_EXECUTION_SLEEPING;
  }
  for (guint i = 0; i < op_budget; ++i) {
    const BosonogaOp *op =
        &g_array_index(program->ops, BosonogaOp, execution->next);
    const BosonogaValue *left = bosonoga_operand_value(&op->left, slots);
    const BosonogaValue *right = bosonoga_operand_value(&op->right, slots);
    ++execution->next;

    switch (op->kind) {
      case BOSONOGA_OP_PRINT_VALUE:
        bosonoga_print_value(output, left);
        break;
      case BOSONOGA_OP_PRINT_NEWLINE:
        g_string_append_c(output, '\n');
        break;
      case BOSONOGA_OP_ASSIGN:
        slots[op->slot] = *left;
        break;
      case BOSONOGA_OP_FLAGS_OR:
        slots[op->slot].flags |= left->flags;
        break;
      case BOSONOGA_OP_CONVERT_TO_F32:
        slots[op->slot].kind = BOSONOGA_VALUE_F32;
        slots[op->slot].f32 = (gfloat)left->i32;
        break;
      case BOSONOGA_OP_CONVERT_TO_I32:
        if (!(left->f32 >= -BOSONOGA_I32_RANGE_LIMIT &&
              left->f32 < BOSONOGA_I32_RANGE_LIMIT)) {
          gchar text[G_ASCII_DTOSTR_BUF_SIZE];
          bosonoga_runtime_error(
              program, op, error, "%s is outside the i32 range",
              bosonoga_format_real(text, (gdouble)left->f32));
          return BOSONOGA_EXECUTION_FAILED;
        }
        slots[op->slot].kind = BOSONOGA_VALUE_I32;
        slots[op->slot].i32 = (gint32)left->f32;
        break;
      // The result is computed first, as the slot can be left, as
      // in set a to_sin a.
      case BOSONOGA_OP_SIN: {
        const gfloat result = (gfloat)sin((gdouble)left->f32);
        slots[op->slot].kind = BOSONOGA_VALUE_F32;
        slots[op->slot].f32 = result;
        break;
      }
      case BOSONOGA_OP_COS: {
        const gfloat result = (gfloat)cos((gdouble)left->f32);
        slots[op->slot].kind = BOSONOGA_VALUE_F32;
        slots[op->slot].f32 = result;
        break;
      }
      case BOSONOGA_OP_ATAN2: {
        const gfloat result =
            (gfloat)atan2((gdouble)left->f32, (gdouble)right->f32);
        slots[op->slot].kind = BOSONOGA_VALUE_F32;
        slots[op->slot].f32 = result;
        break;
      }
      case BOSONOGA_OP_SQRT: {
        if (left->f32 < 0.0f) {
          gchar text[G_ASCII_DTOSTR_BUF_SIZE];
          bosonoga_runtime_error(
              program, op, error, "to_sqrt of a negative number (%s)",
              bosonoga_format_real(text, (gdouble)left->f32));
          return BOSONOGA_EXECUTION_FAILED;
        }
        const gfloat result = (gfloat)sqrt((gdouble)left->f32);
        slots[op->slot].kind = BOSONOGA_VALUE_F32;
        slots[op->slot].f32 = result;
        break;
      }
      case BOSONOGA_OP_POW: {
        const gdouble result = pow((gdouble)left->f32, (gdouble)right->f32);
        if (isnan(result)) {
          gchar base[G_ASCII_DTOSTR_BUF_SIZE];
          gchar exponent[G_ASCII_DTOSTR_BUF_SIZE];
          bosonoga_runtime_error(
              program, op, error,
              "to_pow of a negative number (%s) to a fractional power (%s)",
              bosonoga_format_real(base, (gdouble)left->f32),
              bosonoga_format_real(exponent, (gdouble)right->f32));
          return BOSONOGA_EXECUTION_FAILED;
        }
        if (!(ABS(result) <= (gdouble)G_MAXFLOAT)) {
          gchar text[G_ASCII_DTOSTR_BUF_SIZE];
          bosonoga_runtime_error(program, op, error,
                                 "the result (%s) is outside the f32 range",
                                 bosonoga_format_real(text, result));
          return BOSONOGA_EXECUTION_FAILED;
        }
        slots[op->slot].kind = BOSONOGA_VALUE_F32;
        slots[op->slot].f32 = (gfloat)result;
        break;
      }
      case BOSONOGA_OP_ACCUMULATOR_LOAD:
        if (left->kind == BOSONOGA_VALUE_F32) {
          accumulator->real = (gdouble)left->f32;
        } else {
          accumulator->whole = left->i32;
        }
        break;
      case BOSONOGA_OP_ACCUMULATOR_ADD:
        if (left->kind == BOSONOGA_VALUE_F32) {
          accumulator->real += (gdouble)left->f32;
        } else {
          accumulator->whole += left->i32;
        }
        break;
      case BOSONOGA_OP_ACCUMULATOR_SUBTRACT:
        if (left->kind == BOSONOGA_VALUE_F32) {
          accumulator->real -= (gdouble)left->f32;
        } else {
          accumulator->whole -= left->i32;
        }
        break;
      case BOSONOGA_OP_ACCUMULATOR_MULTIPLY:
        if (left->kind == BOSONOGA_VALUE_F32) {
          accumulator->real *= (gdouble)left->f32;
        } else if (__builtin_mul_overflow(accumulator->whole, (gint64)left->i32,
                                          &accumulator->whole)) {
          bosonoga_runtime_error(program, op, error,
                                 "the result is outside the i32 range");
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_ACCUMULATOR_DIVIDE:
      case BOSONOGA_OP_ACCUMULATOR_MOD:
        if (!bosonoga_accumulator_divide(program, op, accumulator, left,
                                         error)) {
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_ACCUMULATOR_STORE:
        if (!bosonoga_accumulator_store(program, op, accumulator,
                                        &slots[op->slot], error)) {
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_JUMP:
        execution->next = op->target;
        break;
      case BOSONOGA_OP_JUMP_UNLESS:
        if (bosonoga_compare(op->compare, left, right) ? op->negate
                                                       : !op->negate) {
          execution->next = op->target;
        }
        break;
      case BOSONOGA_OP_LOOP_TEST:
        if (slots[op->slot].i32 >= op->integer) {
          execution->next = op->target;
        }
        break;
      case BOSONOGA_OP_LOOP_NEXT:
        ++slots[op->slot].i32;
        execution->next = op->target;
        break;
      case BOSONOGA_OP_SLEEP:
        execution->wake_time =
            g_get_monotonic_time() +
            (gint64)op->milliseconds * G_TIME_SPAN_MILLISECOND;
        return BOSONOGA_EXECUTION_SLEEPING;
      case BOSONOGA_OP_RETURN:
        *exit_status = op->integer;
        return BOSONOGA_EXECUTION_RETURNED;
      case BOSONOGA_OP_MISSING_RETURN:
        bosonoga_runtime_error(program, op, error,
                               "the program ended without a return");
        return BOSONOGA_EXECUTION_FAILED;
      case BOSONOGA_OP_SECTION_END:
        return BOSONOGA_EXECUTION_RETURNED;
      case BOSONOGA_OP_GFX_CLEAR:
        scene->clear_color = left->color;
        break;
      case BOSONOGA_OP_DRAW_RECTANGLE: {
        const BosonogaGfxRectangle rectangle = {
            left->f32,
            right->f32,
            bosonoga_operand_value(&op->third, slots)->f32,
            bosonoga_operand_value(&op->fourth, slots)->f32,
            bosonoga_operand_value(&op->fifth, slots)->color,
        };
        g_array_append_val(scene->rectangles, rectangle);
        break;
      }
      case BOSONOGA_OP_GFX_DELTA_TIME:
        slots[op->slot].kind = BOSONOGA_VALUE_F32;
        slots[op->slot].f32 = scene->delta_time;
        break;
    }
  }
  return BOSONOGA_EXECUTION_RUNNING;
}

BosonogaValue *bosonoga_slots_new(const BosonogaProgram *program) {
  return g_new0(BosonogaValue, program->slot_count);
}

BosonogaExecution *bosonoga_execution_new(const BosonogaProgram *program,
                                          BosonogaSection section,
                                          BosonogaValue *slots,
                                          BosonogaGfxScene *scene,
                                          GString *output) {
  BosonogaExecution *execution = g_new0(BosonogaExecution, 1);
  execution->program = program;
  execution->next = program->entries[section];
  execution->slots = slots;
  execution->scene = scene;
  execution->to_stdout = output == NULL;
  execution->output = output == NULL ? g_string_new(NULL) : output;
  return execution;
}

void bosonoga_execution_free(BosonogaExecution *execution) {
  if (execution->to_stdout) {
    g_string_free(execution->output, TRUE);
  }
  g_free(execution);
}

BosonogaExecutionState bosonoga_execution_advance(BosonogaExecution *execution,
                                                  guint op_budget,
                                                  gint *exit_status,
                                                  GError **error) {
  const BosonogaExecutionState state =
      bosonoga_execute(execution, op_budget, exit_status, error);
  if (execution->to_stdout && execution->output->len > 0) {
    g_print("%s", execution->output->str);
    g_string_truncate(execution->output, 0);
  }
  return state;
}

gboolean bosonoga_run(const BosonogaProgram *program, GString *output,
                      gint *exit_status, GError **error) {
  g_autofree BosonogaValue *slots = bosonoga_slots_new(program);
  if (!bosonoga_run_section(program, BOSONOGA_SECTION_GFX_GLOBAL, slots, NULL,
                            output, exit_status, error) ||
      !bosonoga_run_section(program, BOSONOGA_SECTION_GFX_INIT, slots, NULL,
                            output, exit_status, error)) {
    return FALSE;
  }
  const gboolean ran = bosonoga_run_section(
      program, BOSONOGA_SECTION_MAIN, slots, NULL, output, exit_status, error);
  // The error of the main section is the one that is reported.
  g_autoptr(GError) ignored = NULL;
  const gboolean cleaned_up =
      bosonoga_run_section(program, BOSONOGA_SECTION_GFX_CLEANUP, slots, NULL,
                           output, exit_status, ran ? error : &ignored);
  return ran && cleaned_up;
}

gboolean bosonoga_run_section(const BosonogaProgram *program,
                              BosonogaSection section, BosonogaValue *slots,
                              BosonogaGfxScene *scene, GString *output,
                              gint *exit_status, GError **error) {
  g_autoptr(BosonogaExecution) execution =
      bosonoga_execution_new(program, section, slots, scene, output);
  while (TRUE) {
    switch (
        bosonoga_execution_advance(execution, G_MAXUINT, exit_status, error)) {
      case BOSONOGA_EXECUTION_RUNNING:
        break;
      case BOSONOGA_EXECUTION_SLEEPING: {
        const gint64 remaining = execution->wake_time - g_get_monotonic_time();
        if (remaining > 0) {
          g_usleep((gulong)remaining);
        }
        break;
      }
      case BOSONOGA_EXECUTION_RETURNED:
        return TRUE;
      case BOSONOGA_EXECUTION_FAILED:
        return FALSE;
    }
  }
}
