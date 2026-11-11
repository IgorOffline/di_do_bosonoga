#include <stdarg.h>
#include <string.h>

#include "execution.h"

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

static const BosonogaValue *bosonoga_operand_value(
    const BosonogaOperand *operand, const BosonogaValue *frame) {
  return operand->is_variable ? &frame[operand->slot] : &operand->literal;
}

// The numeric errors do not know where in the program they are.
static BosonogaExecutionState bosonoga_fail_at_op(
    const BosonogaProgram *program, const BosonogaOp *op, GError **error) {
  g_prefix_error(error, "%s:%" G_GSIZE_FORMAT ":%" G_GSIZE_FORMAT ": ",
                 program->path, op->line, op->column);
  return BOSONOGA_EXECUTION_FAILED;
}

// The variables of the innermost call, or of the section. A frame in the stack
// moves when the stack grows.
static BosonogaValue *bosonoga_frame(const BosonogaExecution *execution) {
  if (execution->calls->len == 0) {
    return execution->slots;
  }
  const BosonogaCall *call =
      &g_array_index(execution->calls, BosonogaCall, execution->calls->len - 1);
  return &g_array_index(execution->stack, BosonogaValue, call->base);
}

static gboolean bosonoga_call(BosonogaExecution *execution,
                              const BosonogaOp *op, GError **error) {
  const BosonogaFunction *function = &g_array_index(
      execution->program->functions, BosonogaFunction, op->function);
  if (execution->calls->len == BOSONOGA_CALL_DEPTH_MAX) {
    return bosonoga_runtime_error(execution->program, op, error,
                                  "calls are nested more than %d deep",
                                  BOSONOGA_CALL_DEPTH_MAX);
  }
  const BosonogaCall call = {op->function, execution->next,
                             execution->stack->len};
  g_array_append_val(execution->calls, call);
  g_array_set_size(execution->stack, call.base + function->slot_count);
  memcpy(bosonoga_frame(execution), execution->arguments,
         function->parameter_count * sizeof(BosonogaValue));
  execution->next = function->entry;
  return TRUE;
}

static void bosonoga_return(BosonogaExecution *execution) {
  const BosonogaCall call =
      g_array_index(execution->calls, BosonogaCall, execution->calls->len - 1);
  g_array_set_size(execution->calls, execution->calls->len - 1);
  g_array_set_size(execution->stack, call.base);
  execution->next = call.return_to;
}

static gboolean bosonoga_read_array(BosonogaExecution *execution,
                                    const BosonogaOp *op,
                                    const BosonogaValue *index,
                                    GError **error) {
  const BosonogaProgram *program = execution->program;
  const BosonogaFunction *array =
      &g_array_index(program->functions, BosonogaFunction, op->function);
  if (index->i32 < 0 || (guint)index->i32 >= array->row_count) {
    if (array->row_count == 1) {
      return bosonoga_runtime_error(
          program, op, error, "'%.*s' has only row 0, not %" G_GINT32_FORMAT,
          (gint)array->name_length, array->name, index->i32);
    }
    return bosonoga_runtime_error(
        program, op, error, "'%.*s' has rows 0 to %u, not %" G_GINT32_FORMAT,
        (gint)array->name_length, array->name, array->row_count - 1,
        index->i32);
  }
  memcpy(execution->results,
         &g_array_index(
             program->rows, BosonogaValue,
             array->first_value + (guint)index->i32 * array->result_count),
         array->result_count * sizeof(BosonogaValue));
  return TRUE;
}

BosonogaExecutionState bosonoga_execute(BosonogaExecution *execution,
                                        guint op_budget, gint *exit_status,
                                        GError **error) {
  const BosonogaProgram *program = execution->program;
  BosonogaValue *frame = bosonoga_frame(execution);
  GString *output = execution->output;
  BosonogaGfxScene *scene = execution->scene;
  BosonogaAccumulator *accumulator = &execution->accumulator;

  if (g_get_monotonic_time() < execution->wake_time) {
    return BOSONOGA_EXECUTION_SLEEPING;
  }
  for (guint i = 0; i < op_budget; ++i) {
    const BosonogaOp *op =
        &g_array_index(program->ops, BosonogaOp, execution->next);
    const BosonogaValue *left = bosonoga_operand_value(&op->left, frame);
    const BosonogaValue *right = bosonoga_operand_value(&op->right, frame);
    ++execution->next;

    switch (op->kind) {
      case BOSONOGA_OP_PRINT_VALUE:
        bosonoga_print_value(output, left);
        break;
      case BOSONOGA_OP_PRINT_NEWLINE:
        g_string_append_c(output, '\n');
        break;
      case BOSONOGA_OP_ASSIGN:
        frame[op->slot] = *left;
        break;
      case BOSONOGA_OP_FLAGS_OR:
        frame[op->slot].flags |= left->flags;
        break;
      case BOSONOGA_OP_CONVERT_TO_I32:
      case BOSONOGA_OP_CONVERT_TO_I64:
      case BOSONOGA_OP_CONVERT_TO_U32:
      case BOSONOGA_OP_CONVERT_TO_U64:
      case BOSONOGA_OP_CONVERT_TO_F32:
      case BOSONOGA_OP_CONVERT_TO_F64:
        if (!bosonoga_convert(op->kind, left, &frame[op->slot], error)) {
          return bosonoga_fail_at_op(program, op, error);
        }
        break;
      case BOSONOGA_OP_ABS:
      case BOSONOGA_OP_MIN:
      case BOSONOGA_OP_MAX:
      case BOSONOGA_OP_FLOOR:
      case BOSONOGA_OP_CEIL:
      case BOSONOGA_OP_ROUND:
      case BOSONOGA_OP_SIN:
      case BOSONOGA_OP_COS:
      case BOSONOGA_OP_TAN:
      case BOSONOGA_OP_ASIN:
      case BOSONOGA_OP_ACOS:
      case BOSONOGA_OP_ATAN2:
      case BOSONOGA_OP_SQRT:
      case BOSONOGA_OP_POW:
      case BOSONOGA_OP_LOG:
      case BOSONOGA_OP_LOG10:
      case BOSONOGA_OP_EXP:
      case BOSONOGA_OP_LOG1P:
      case BOSONOGA_OP_EXPM1:
      case BOSONOGA_OP_HYPOT:
        if (!bosonoga_math(op->kind, left, right, &frame[op->slot], error)) {
          return bosonoga_fail_at_op(program, op, error);
        }
        break;
      case BOSONOGA_OP_ACCUMULATOR_LOAD:
        bosonoga_accumulator_load(accumulator, left);
        break;
      case BOSONOGA_OP_ACCUMULATOR_ADD:
      case BOSONOGA_OP_ACCUMULATOR_SUBTRACT:
      case BOSONOGA_OP_ACCUMULATOR_MULTIPLY:
      case BOSONOGA_OP_ACCUMULATOR_DIVIDE:
      case BOSONOGA_OP_ACCUMULATOR_MOD:
        if (!bosonoga_accumulator_step(accumulator, op->kind, left, error)) {
          return bosonoga_fail_at_op(program, op, error);
        }
        break;
      case BOSONOGA_OP_ACCUMULATOR_STORE:
        if (!bosonoga_accumulator_store(accumulator, &frame[op->slot], error)) {
          return bosonoga_fail_at_op(program, op, error);
        }
        break;
      case BOSONOGA_OP_JUMP:
        execution->next = op->target;
        break;
      case BOSONOGA_OP_JUMP_UNLESS:
        if (bosonoga_compare_values(op->compare, left, right) ? op->negate
                                                              : !op->negate) {
          execution->next = op->target;
        }
        break;
      case BOSONOGA_OP_LOOP_TEST:
        if (frame[op->slot].i32 >= op->integer) {
          execution->next = op->target;
        }
        break;
      case BOSONOGA_OP_LOOP_NEXT:
        ++frame[op->slot].i32;
        execution->next = op->target;
        break;
      case BOSONOGA_OP_SLEEP:
        execution->wake_time = g_get_monotonic_time() +
                               (gint64)left->u32 * G_TIME_SPAN_MILLISECOND;
        return BOSONOGA_EXECUTION_SLEEPING;
      case BOSONOGA_OP_RETURN:
        *exit_status = op->integer;
        return BOSONOGA_EXECUTION_RETURNED;
      case BOSONOGA_OP_MISSING_RETURN:
        if (execution->calls->len > 0) {
          const BosonogaFunction *function =
              &g_array_index(program->functions, BosonogaFunction,
                             g_array_index(execution->calls, BosonogaCall,
                                           execution->calls->len - 1)
                                 .function);
          bosonoga_runtime_error(program, op, error,
                                 "the function '%.*s' ended without a return",
                                 (gint)function->name_length, function->name);
        } else {
          bosonoga_runtime_error(program, op, error,
                                 "the program ended without a return");
        }
        return BOSONOGA_EXECUTION_FAILED;
      case BOSONOGA_OP_SECTION_END:
        return BOSONOGA_EXECUTION_RETURNED;
      case BOSONOGA_OP_GFX_CLEAR:
        scene->clear_color = left->color;
        break;
      case BOSONOGA_OP_DRAW_RECTANGLE: {
        const BosonogaGfxShape rectangle = {
            .kind = BOSONOGA_GFX_RECTANGLE,
            .x = left->f32,
            .y = right->f32,
            .width = bosonoga_operand_value(&op->third, frame)->f32,
            .height = bosonoga_operand_value(&op->fourth, frame)->f32,
            .color = bosonoga_operand_value(&op->fifth, frame)->color,
        };
        g_array_append_val(scene->shapes, rectangle);
        break;
      }
      case BOSONOGA_OP_DRAW_HEX: {
        const BosonogaGfxShape hex = {
            .kind = BOSONOGA_GFX_HEX,
            .x = left->f32,
            .y = right->f32,
            .radius = bosonoga_operand_value(&op->third, frame)->f32,
            .color = bosonoga_operand_value(&op->fourth, frame)->color,
        };
        if (hex.radius > 0.0f) {
          g_array_append_val(scene->shapes, hex);
        }
        break;
      }
      case BOSONOGA_OP_DRAW_CIRCLE: {
        const BosonogaGfxShape circle = {
            .kind = BOSONOGA_GFX_CIRCLE,
            .x = left->f32,
            .y = right->f32,
            .radius = bosonoga_operand_value(&op->third, frame)->f32,
            .color = bosonoga_operand_value(&op->fourth, frame)->color,
        };
        if (circle.radius > 0.0f) {
          g_array_append_val(scene->shapes, circle);
        }
        break;
      }
      case BOSONOGA_OP_DRAW_TEXT_PART:
        bosonoga_print_value(execution->join, left);
        if (execution->join->len > BOSONOGA_DRAW_TEXT_LENGTH_MAX) {
          bosonoga_runtime_error(program, op, error,
                                 "gfx_draw_text is longer than %d bytes",
                                 BOSONOGA_DRAW_TEXT_LENGTH_MAX);
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_DRAW_TEXT: {
        GString *join = execution->join;
        const BosonogaGfxShape text = {
            .kind = BOSONOGA_GFX_TEXT,
            .x = left->f32,
            .y = right->f32,
            .font = bosonoga_operand_value(&op->third, frame)->font.index,
            .size = bosonoga_operand_value(&op->fourth, frame)->f32,
            .color = bosonoga_operand_value(&op->fifth, frame)->color,
            .align = (guint32)op->integer,
            .text = scene->text->len,
            .text_length = join->len,
        };
        if (text.size > 0.0f) {
          g_string_append_len(scene->text, join->str, (gssize)join->len);
          g_array_append_val(scene->shapes, text);
        }
        g_string_truncate(join, 0);
        break;
      }
      case BOSONOGA_OP_ARGUMENT:
        execution->arguments[op->index] = *left;
        break;
      case BOSONOGA_OP_CALL:
        if (!bosonoga_call(execution, op, error)) {
          return BOSONOGA_EXECUTION_FAILED;
        }
        frame = bosonoga_frame(execution);
        break;
      case BOSONOGA_OP_RESULT:
        execution->results[op->index] = *left;
        break;
      case BOSONOGA_OP_FUNCTION_RETURN:
        bosonoga_return(execution);
        frame = bosonoga_frame(execution);
        break;
      case BOSONOGA_OP_TAKE_RESULT:
        frame[op->slot] = execution->results[op->index];
        break;
      case BOSONOGA_OP_ARRAY_READ:
        if (!bosonoga_read_array(execution, op, left, error)) {
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_NEW_STR_PART:
        bosonoga_print_value(execution->join, left);
        if (execution->join->len > BOSONOGA_NEW_STR_LENGTH_MAX) {
          bosonoga_runtime_error(program, op, error,
                                 "new str of is longer than %d bytes",
                                 BOSONOGA_NEW_STR_LENGTH_MAX);
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_NEW_STR: {
        GString *join = execution->join;
        gchar *text = g_strndup(join->str, join->len);
        g_ptr_array_add(execution->owned, text);
        frame[op->slot].kind = BOSONOGA_VALUE_STR;
        frame[op->slot].str.text = text;
        frame[op->slot].str.length = join->len;
        g_string_truncate(join, 0);
        break;
      }
      case BOSONOGA_OP_UNSET: {
        GPtrArray *owned = execution->owned;
        for (guint j = 0; j < owned->len; ++j) {
          if (g_ptr_array_index(owned, j) == frame[op->slot].str.text) {
            g_ptr_array_remove_index_fast(owned, j);
            break;
          }
        }
        memset(&frame[op->slot], 0, sizeof(frame[op->slot]));
        break;
      }
      case BOSONOGA_OP_GFX_DELTA_TIME:
        frame[op->slot].kind = BOSONOGA_VALUE_F32;
        frame[op->slot].f32 = scene->delta_time;
        break;
      case BOSONOGA_OP_GFX_KEY:
        frame[op->slot].kind = BOSONOGA_VALUE_INPUT_KEY;
        frame[op->slot].input_key = scene->key;
        break;
    }
  }
  return BOSONOGA_EXECUTION_RUNNING;
}
