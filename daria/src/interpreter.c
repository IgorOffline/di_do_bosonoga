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
// result that is stored has to fit. A whole number that does not even fit here
// is an error when it is computed.
typedef struct {
  gint64 whole;
  gdouble real;
} DariaAccumulator;

struct _DariaExecution {
  const DariaProgram *program;
  DariaValue *slots;
  DariaAccumulator accumulator;
  guint next;        // Index of the op to run.
  gint64 wake_time;  // In monotonic microseconds, a sleep ends at this time.
  DariaGfxScene *scene;  // What the gfx statements record goes here, or NULL.
  GString *output;       // What the info statements print goes here.
  gboolean to_stdout;  // Whether output is a buffer for stdout, which is owned.
};

static gboolean daria_accumulator_store(const DariaProgram *program,
                                        const DariaOp *op,
                                        const DariaAccumulator *accumulator,
                                        DariaValue *slot, GError **error) {
  if (slot->kind == DARIA_VALUE_F32) {
    // Written so that NaN, from infinity times 0, is outside the range too.
    if (!(ABS(accumulator->real) <= (gdouble)G_MAXFLOAT)) {
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
    case DARIA_VALUE_COLOR:
      g_string_append_printf(output, "%06" G_GINT32_MODIFIER "X", value->color);
      break;
  }
}

static DariaExecutionState daria_execute(DariaExecution *execution,
                                         guint op_budget, gint *exit_status,
                                         GError **error) {
  const DariaProgram *program = execution->program;
  DariaValue *slots = execution->slots;
  GString *output = execution->output;
  DariaGfxScene *scene = execution->scene;
  DariaAccumulator *accumulator = &execution->accumulator;

  if (g_get_monotonic_time() < execution->wake_time) {
    return DARIA_EXECUTION_SLEEPING;
  }
  for (guint i = 0; i < op_budget; ++i) {
    const DariaOp *op = &g_array_index(program->ops, DariaOp, execution->next);
    const DariaValue *left = daria_operand_value(&op->left, slots);
    const DariaValue *right = daria_operand_value(&op->right, slots);
    ++execution->next;

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
          daria_runtime_error(program, op, error, "%s is outside the i32 range",
                              daria_format_real(text, (gdouble)left->f32));
          return DARIA_EXECUTION_FAILED;
        }
        slots[op->slot].kind = DARIA_VALUE_I32;
        slots[op->slot].i32 = (gint32)left->f32;
        break;
      case DARIA_OP_ACCUMULATOR_LOAD:
        if (left->kind == DARIA_VALUE_F32) {
          accumulator->real = (gdouble)left->f32;
        } else {
          accumulator->whole = left->i32;
        }
        break;
      case DARIA_OP_ACCUMULATOR_ADD:
        if (left->kind == DARIA_VALUE_F32) {
          accumulator->real += (gdouble)left->f32;
        } else {
          accumulator->whole += left->i32;
        }
        break;
      case DARIA_OP_ACCUMULATOR_SUBTRACT:
        if (left->kind == DARIA_VALUE_F32) {
          accumulator->real -= (gdouble)left->f32;
        } else {
          accumulator->whole -= left->i32;
        }
        break;
      case DARIA_OP_ACCUMULATOR_MULTIPLY:
        if (left->kind == DARIA_VALUE_F32) {
          accumulator->real *= (gdouble)left->f32;
        } else if (__builtin_mul_overflow(accumulator->whole, (gint64)left->i32,
                                          &accumulator->whole)) {
          daria_runtime_error(program, op, error,
                              "the result is outside the i32 range");
          return DARIA_EXECUTION_FAILED;
        }
        break;
      case DARIA_OP_ACCUMULATOR_DIVIDE:
        if (left->kind == DARIA_VALUE_F32
                ? !(left->f32 < 0.0f || left->f32 > 0.0f)
                : left->i32 == 0) {
          daria_runtime_error(program, op, error, "division by zero");
          return DARIA_EXECUTION_FAILED;
        }
        if (left->kind == DARIA_VALUE_F32) {
          accumulator->real /= (gdouble)left->f32;
        } else {
          accumulator->whole /= left->i32;
        }
        break;
      case DARIA_OP_ACCUMULATOR_STORE:
        if (!daria_accumulator_store(program, op, accumulator, &slots[op->slot],
                                     error)) {
          return DARIA_EXECUTION_FAILED;
        }
        break;
      case DARIA_OP_JUMP:
        execution->next = op->target;
        break;
      case DARIA_OP_JUMP_UNLESS:
        if (!daria_compare(op->compare, left, right)) {
          execution->next = op->target;
        }
        break;
      case DARIA_OP_LOOP_TEST:
        if (slots[op->slot].i32 >= op->integer) {
          execution->next = op->target;
        }
        break;
      case DARIA_OP_LOOP_NEXT:
        ++slots[op->slot].i32;
        execution->next = op->target;
        break;
      case DARIA_OP_SLEEP:
        execution->wake_time =
            g_get_monotonic_time() +
            (gint64)op->milliseconds * G_TIME_SPAN_MILLISECOND;
        return DARIA_EXECUTION_SLEEPING;
      case DARIA_OP_RETURN:
        *exit_status = op->integer;
        return DARIA_EXECUTION_RETURNED;
      case DARIA_OP_MISSING_RETURN:
        daria_runtime_error(program, op, error,
                            "the program ended without a return");
        return DARIA_EXECUTION_FAILED;
      case DARIA_OP_SECTION_END:
        return DARIA_EXECUTION_RETURNED;
      case DARIA_OP_GFX_CLEAR:
        scene->clear_color = left->color;
        break;
      case DARIA_OP_DRAW_RECTANGLE: {
        const DariaGfxRectangle rectangle = {
            left->f32,
            right->f32,
            daria_operand_value(&op->third, slots)->f32,
            daria_operand_value(&op->fourth, slots)->f32,
            daria_operand_value(&op->fifth, slots)->color,
        };
        g_array_append_val(scene->rectangles, rectangle);
        break;
      }
    }
  }
  return DARIA_EXECUTION_RUNNING;
}

DariaExecution *daria_execution_new(const DariaProgram *program,
                                    DariaSection section, DariaGfxScene *scene,
                                    GString *output) {
  DariaExecution *execution = g_new0(DariaExecution, 1);
  execution->program = program;
  execution->next = program->entries[section];
  execution->slots = g_new0(DariaValue, program->slot_count);
  execution->scene = scene;
  execution->to_stdout = output == NULL;
  execution->output = output == NULL ? g_string_new(NULL) : output;
  return execution;
}

void daria_execution_free(DariaExecution *execution) {
  if (execution->to_stdout) {
    g_string_free(execution->output, TRUE);
  }
  g_free(execution->slots);
  g_free(execution);
}

DariaExecutionState daria_execution_advance(DariaExecution *execution,
                                            guint op_budget, gint *exit_status,
                                            GError **error) {
  const DariaExecutionState state =
      daria_execute(execution, op_budget, exit_status, error);
  if (execution->to_stdout && execution->output->len > 0) {
    g_print("%s", execution->output->str);
    g_string_truncate(execution->output, 0);
  }
  return state;
}

gboolean daria_run(const DariaProgram *program, GString *output,
                   gint *exit_status, GError **error) {
  if (!daria_run_section(program, DARIA_SECTION_GFX_INIT, NULL, output,
                         exit_status, error)) {
    return FALSE;
  }
  const gboolean ran = daria_run_section(program, DARIA_SECTION_MAIN, NULL,
                                         output, exit_status, error);
  // The error of the main section is the one that is reported.
  g_autoptr(GError) ignored = NULL;
  const gboolean cleaned_up =
      daria_run_section(program, DARIA_SECTION_GFX_CLEANUP, NULL, output,
                        exit_status, ran ? error : &ignored);
  return ran && cleaned_up;
}

gboolean daria_run_section(const DariaProgram *program, DariaSection section,
                           DariaGfxScene *scene, GString *output,
                           gint *exit_status, GError **error) {
  g_autoptr(DariaExecution) execution =
      daria_execution_new(program, section, scene, output);
  while (TRUE) {
    switch (daria_execution_advance(execution, G_MAXUINT, exit_status, error)) {
      case DARIA_EXECUTION_RUNNING:
        break;
      case DARIA_EXECUTION_SLEEPING: {
        const gint64 remaining = execution->wake_time - g_get_monotonic_time();
        if (remaining > 0) {
          g_usleep((gulong)remaining);
        }
        break;
      }
      case DARIA_EXECUTION_RETURNED:
        return TRUE;
      case DARIA_EXECUTION_FAILED:
        return FALSE;
    }
  }
}
