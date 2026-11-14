#include <stdarg.h>
#include <string.h>

#include "execution.h"
#include "io.h"
#include "xml.h"

static gboolean bosonoga_runtime_error(const BosonogaOp *op, GError **error,
                                       const gchar *format, ...)
    G_GNUC_PRINTF(3, 4);

static gboolean bosonoga_runtime_error(const BosonogaOp *op, GError **error,
                                       const gchar *format, ...) {
  va_list args;
  va_start(args, format);
  g_autofree gchar *message = g_strdup_vprintf(format, args);
  va_end(args);
  g_set_error(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
              "%s:%" G_GSIZE_FORMAT ":%" G_GSIZE_FORMAT ": %s", op->path,
              op->line, op->column, message);
  return FALSE;
}

static const BosonogaValue *bosonoga_operand_value(
    const BosonogaOperand *operand, const BosonogaValue *frame) {
  return operand->is_variable ? &frame[operand->slot] : &operand->literal;
}

// The numeric errors do not know where in the program they are.
static BosonogaExecutionState bosonoga_fail_at_op(const BosonogaOp *op,
                                                  GError **error) {
  g_prefix_error(error, "%s:%" G_GSIZE_FORMAT ":%" G_GSIZE_FORMAT ": ",
                 op->path, op->line, op->column);
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
    return bosonoga_runtime_error(op, error,
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

static gboolean bosonoga_check_row(const BosonogaOp *op,
                                   const BosonogaFunction *array,
                                   const BosonogaValue *index, GError **error) {
  if (index->i32 < 0 || (guint)index->i32 >= array->row_count) {
    if (array->row_count == 1) {
      return bosonoga_runtime_error(
          op, error, "'%.*s' has only row 0, not %" G_GINT32_FORMAT,
          (gint)array->name_length, array->name, index->i32);
    }
    return bosonoga_runtime_error(
        op, error, "'%.*s' has rows 0 to %u, not %" G_GINT32_FORMAT,
        (gint)array->name_length, array->name, array->row_count - 1,
        index->i32);
  }
  return TRUE;
}

static void bosonoga_init_array(const BosonogaProgram *program,
                                const BosonogaOp *op, BosonogaValue *frame) {
  const BosonogaFunction *array =
      &g_array_index(program->functions, BosonogaFunction, op->function);
  const BosonogaValue *rows =
      &g_array_index(program->rows, BosonogaValue, array->first_value);
  const gsize row_size = array->result_count * sizeof(BosonogaValue);
  if (!array->is_zeroed) {
    memcpy(&frame[array->slot], rows, array->row_count * row_size);
    return;
  }
  for (guint i = 0; i < array->row_count; ++i) {
    memcpy(&frame[array->slot + i * array->result_count], rows, row_size);
  }
}

static gboolean bosonoga_read_array(BosonogaExecution *execution,
                                    const BosonogaOp *op,
                                    const BosonogaValue *index,
                                    const BosonogaValue *frame,
                                    GError **error) {
  const BosonogaProgram *program = execution->program;
  const BosonogaFunction *array =
      &g_array_index(program->functions, BosonogaFunction, op->function);
  if (!bosonoga_check_row(op, array, index, error)) {
    return FALSE;
  }
  const guint offset = (guint)index->i32 * array->result_count;
  memcpy(execution->results,
         array->is_var ? &frame[array->slot + offset]
                       : &g_array_index(program->rows, BosonogaValue,
                                        array->first_value + offset),
         array->result_count * sizeof(BosonogaValue));
  return TRUE;
}

static gboolean bosonoga_write_array(const BosonogaExecution *execution,
                                     const BosonogaOp *op,
                                     const BosonogaValue *index,
                                     BosonogaValue *frame, GError **error) {
  const BosonogaProgram *program = execution->program;
  const BosonogaFunction *array =
      &g_array_index(program->functions, BosonogaFunction, op->function);
  if (!bosonoga_check_row(op, array, index, error)) {
    return FALSE;
  }
  memcpy(&frame[array->slot + (guint)index->i32 * array->result_count],
         execution->arguments, array->result_count * sizeof(BosonogaValue));
  return TRUE;
}

// What new, io_read or an xml_ statement made, if it did.
static void bosonoga_free_owned(BosonogaExecution *execution,
                                const gchar *text) {
  GPtrArray *owned = execution->owned;
  for (guint i = 0; i < owned->len; ++i) {
    if (g_ptr_array_index(owned, i) == text) {
      g_ptr_array_remove_index_fast(owned, i);
      return;
    }
  }
}

static const gchar *bosonoga_own(BosonogaExecution *execution,
                                 const gchar *text, gsize length) {
  gchar *copy = g_strndup(text, length);
  g_ptr_array_add(execution->owned, copy);
  return copy;
}

static void bosonoga_give_bool(BosonogaExecution *execution, guint index,
                               gboolean value) {
  BosonogaValue *result = &execution->results[index];
  memset(result, 0, sizeof(*result));
  result->kind = BOSONOGA_VALUE_BOOL;
  result->boolean = value;
}

// A copy that the execution owns, if the block takes it, or else nothing.
static void bosonoga_give_text(BosonogaExecution *execution,
                               const BosonogaOp *op, guint index,
                               BosonogaValueKind kind, const gchar *text,
                               gsize length) {
  BosonogaValue *result = &execution->results[index];
  memset(result, 0, sizeof(*result));
  result->kind = kind;
  result->str.text = "";
  if ((op->integer & (1 << index)) != 0 && length > 0) {
    result->str.text = bosonoga_own(execution, text, length);
    result->str.length = length;
  }
}

// Gives the reason as the str at index, and frees it.
static void bosonoga_give_reason(BosonogaExecution *execution,
                                 const BosonogaOp *op, guint index,
                                 gchar *reason) {
  bosonoga_give_text(execution, op, index, BOSONOGA_VALUE_STR,
                     reason != NULL ? reason : "",
                     reason != NULL ? strlen(reason) : 0);
  g_free(reason);
}

// The file that a dir and a name of io_read, io_write or io_modified name.
static gchar *bosonoga_io_path(const BosonogaExecution *execution,
                               const BosonogaOp *op, const BosonogaValue *dir,
                               const BosonogaValue *name, GError **error) {
  if (!bosonoga_io_is_file_name(name->str.text, name->str.length)) {
    g_autofree gchar *shown =
        g_utf8_make_valid(name->str.text, (gssize)MIN(name->str.length, 64));
    bosonoga_runtime_error(op, error,
                           "'%s' is not a file name, which is 1 to %d ASCII "
                           "letters, digits, _, - and ., and does not start "
                           "with .",
                           shown, BOSONOGA_IO_FILE_NAME_MAX);
    return NULL;
  }
  g_autofree gchar *file = g_strndup(name->str.text, name->str.length);
  return g_build_filename(g_array_index(execution->program->dirs,
                                        BosonogaProgramAsset, dir->asset.index)
                              .path,
                          file, NULL);
}

static gboolean bosonoga_xml_part(BosonogaExecution *execution,
                                  const BosonogaOp *op,
                                  const BosonogaValue *value, GError **error) {
  GString *join = execution->join;
  switch ((BosonogaXmlPart)op->integer) {
    case BOSONOGA_XML_PART_RAW:
      g_string_append_len(join, value->str.text, (gssize)value->str.length);
      break;
    case BOSONOGA_XML_PART_TEXT:
    case BOSONOGA_XML_PART_VALUE: {
      g_autoptr(GString) printed = g_string_new(NULL);
      bosonoga_print_value(printed, value);
      if (!g_utf8_validate_len(printed->str, printed->len, NULL) ||
          memchr(printed->str, '\0', printed->len) != NULL) {
        return bosonoga_runtime_error(
            op, error, "an xml_str holds UTF-8 text without NUL bytes");
      }
      bosonoga_xml_escape(join, printed->str, printed->len);
      if (op->integer == BOSONOGA_XML_PART_VALUE) {
        g_string_append_c(join, '"');
      }
      break;
    }
    case BOSONOGA_XML_PART_OPEN:
      g_string_append_c(join, '<');
      g_string_append_len(join, value->str.text, (gssize)value->str.length);
      break;
    case BOSONOGA_XML_PART_NAME:
      g_string_append_c(join, ' ');
      g_string_append_len(join, value->str.text, (gssize)value->str.length);
      g_string_append(join, "=\"");
      break;
    case BOSONOGA_XML_PART_START:
      g_string_append_c(join, '>');
      break;
    case BOSONOGA_XML_PART_EMPTY:
      g_string_append(join, "/>");
      break;
    case BOSONOGA_XML_PART_CLOSE:
      g_string_append(join, "</");
      g_string_append_len(join, value->str.text, (gssize)value->str.length);
      g_string_append_c(join, '>');
      break;
  }
  if (join->len > BOSONOGA_XML_STR_LENGTH_MAX) {
    return bosonoga_runtime_error(op, error,
                                  "an xml_str is longer than %d bytes",
                                  BOSONOGA_XML_STR_LENGTH_MAX);
  }
  return TRUE;
}

static void bosonoga_join_store(BosonogaExecution *execution,
                                const BosonogaOp *op, BosonogaValue *frame) {
  GString *join = execution->join;
  BosonogaValue *slot = &frame[op->slot];
  const gchar *previous = slot->str.text;
  slot->kind = op->made;
  slot->str.text = bosonoga_own(execution, join->str, join->len);
  slot->str.length = join->len;
  bosonoga_free_owned(execution, previous);
  g_string_truncate(join, 0);
}

static gboolean bosonoga_io_read_op(BosonogaExecution *execution,
                                    const BosonogaOp *op,
                                    const BosonogaValue *frame,
                                    GError **error) {
  g_autofree gchar *path =
      bosonoga_io_path(execution, op, bosonoga_operand_value(&op->left, frame),
                       bosonoga_operand_value(&op->right, frame), error);
  if (path == NULL) {
    return FALSE;
  }
  g_autofree gchar *contents = NULL;
  gsize length = 0;
  gchar *reason = NULL;
  gboolean is_read = bosonoga_io_read(path, BOSONOGA_XML_STR_LENGTH_MAX,
                                      &contents, &length, &reason);
  gsize start = 0;
  // Only a text that the block takes is checked.
  if (is_read && (op->integer & 2) != 0) {
    g_autofree gchar *shown = g_filename_display_name(path);
    if (op->made == BOSONOGA_VALUE_STR) {
      if (!g_utf8_validate_len(contents, length, NULL) ||
          memchr(contents, '\0', length) != NULL) {
        is_read = FALSE;
        reason =
            g_strdup_printf("%s: is not UTF-8 text without NUL bytes", shown);
      }
    } else {
      GError *xml_error = NULL;
      if (!bosonoga_xml_check_document(contents, length, &start, &length,
                                       &xml_error)) {
        is_read = FALSE;
        reason = g_strdup_printf("%s: is not an XML document: %s", shown,
                                 xml_error->message);
        g_error_free(xml_error);
      }
    }
  }
  bosonoga_give_bool(execution, 0, is_read);
  bosonoga_give_text(execution, op, 1, op->made,
                     is_read ? contents + start : "", is_read ? length : 0);
  bosonoga_give_reason(execution, op, 2, reason);
  return TRUE;
}

static gboolean bosonoga_io_write_op(BosonogaExecution *execution,
                                     const BosonogaOp *op,
                                     const BosonogaValue *frame,
                                     GError **error) {
  const BosonogaValue *dir = bosonoga_operand_value(&op->left, frame);
  const BosonogaValue *value = bosonoga_operand_value(&op->third, frame);
  g_autofree gchar *path = bosonoga_io_path(
      execution, op, dir, bosonoga_operand_value(&op->right, frame), error);
  if (path == NULL) {
    return FALSE;
  }
  g_autoptr(GString) text = g_string_new(NULL);
  if (value->kind == BOSONOGA_VALUE_XML_STR) {
    if (!bosonoga_xml_is_element(value->str.text, value->str.length)) {
      return bosonoga_runtime_error(op, error,
                                    "io_write writes an xml_str of one "
                                    "element, the root of its document");
    }
    g_string_append(text, BOSONOGA_XML_DECLARATION "\n");
    g_string_append_len(text, value->str.text, (gssize)value->str.length);
    g_string_append_c(text, '\n');
  } else {
    g_string_append_len(text, value->str.text, (gssize)value->str.length);
  }
  gboolean is_taken = FALSE;
  gchar *reason = NULL;
  const gboolean is_saved =
      bosonoga_io_write(g_array_index(execution->program->dirs,
                                      BosonogaProgramAsset, dir->asset.index)
                            .path,
                        path, text->str, text->len, &is_taken, &reason);
  bosonoga_give_bool(execution, 0, is_saved);
  bosonoga_give_bool(execution, 1, is_taken);
  bosonoga_give_reason(execution, op, 2, reason);
  return TRUE;
}

static gboolean bosonoga_io_modified_op(BosonogaExecution *execution,
                                        const BosonogaOp *op,
                                        const BosonogaValue *frame,
                                        GError **error) {
  g_autofree gchar *path =
      bosonoga_io_path(execution, op, bosonoga_operand_value(&op->left, frame),
                       bosonoga_operand_value(&op->right, frame), error);
  if (path == NULL) {
    return FALSE;
  }
  gint64 modified = 0;
  bosonoga_give_bool(execution, 0, bosonoga_io_modified(path, &modified));
  BosonogaValue *result = &execution->results[1];
  memset(result, 0, sizeof(*result));
  result->kind = BOSONOGA_VALUE_I64;
  result->i64 = modified;
  return TRUE;
}

static gboolean bosonoga_xml_read_op(BosonogaExecution *execution,
                                     const BosonogaOp *op,
                                     const BosonogaValue *frame,
                                     GError **error) {
  const BosonogaValue *xml = bosonoga_operand_value(&op->left, frame);
  const BosonogaValue *name = bosonoga_operand_value(&op->right, frame);
  const gboolean is_child = op->kind == BOSONOGA_OP_XML_CHILD;
  if (!bosonoga_xml_is_element(xml->str.text, xml->str.length)) {
    return bosonoga_runtime_error(op, error,
                                  "%s reads an xml_str of one element",
                                  is_child ? "xml_child" : "xml_attribute");
  }
  if (is_child) {
    const gint32 index = bosonoga_operand_value(&op->third, frame)->i32;
    if (index < 0) {
      return bosonoga_runtime_error(
          op, error, "xml_child takes an index from 0, not %" G_GINT32_FORMAT,
          index);
    }
    gsize child = 0;
    gsize child_length = 0;
    const gboolean is_found = bosonoga_xml_child(
        xml->str.text, xml->str.length, name->str.text, name->str.length,
        (guint)index, &child, &child_length);
    bosonoga_give_bool(execution, 0, is_found);
    bosonoga_give_text(execution, op, 1, BOSONOGA_VALUE_XML_STR,
                       xml->str.text + child, child_length);
    return TRUE;
  }
  g_autoptr(GString) value = g_string_new(NULL);
  bosonoga_give_bool(
      execution, 0,
      bosonoga_xml_attribute(xml->str.text, xml->str.length, name->str.text,
                             name->str.length, value));
  bosonoga_give_text(execution, op, 1, BOSONOGA_VALUE_STR, value->str,
                     value->len);
  return TRUE;
}

// Decimal digits after an optional -, without a + or a leading 0, as an
// INTEGER is written without its suffix.
static void bosonoga_str_parse_op(BosonogaExecution *execution,
                                  const BosonogaOp *op,
                                  const BosonogaValue *frame) {
  const BosonogaValue *text = bosonoga_operand_value(&op->left, frame);
  BosonogaValue *number = &execution->results[1];
  memset(number, 0, sizeof(*number));
  number->kind = op->made;
  const gsize sign = text->str.length > 0 && text->str.text[0] == '-';
  const gsize digits = text->str.length - sign;
  gboolean is_parsed = digits > 0 && digits <= BOSONOGA_WIDE_TEXT_SIZE &&
                       !(digits > 1 && text->str.text[sign] == '0');
  for (gsize i = sign; is_parsed && i < text->str.length; ++i) {
    is_parsed = g_ascii_isdigit(text->str.text[i]);
  }
  if (is_parsed) {
    g_autofree gchar *copy = g_strndup(text->str.text, text->str.length);
    gint64 negative = 0;
    guint64 positive = 0;
    BosonogaValue parsed;
    is_parsed = sign ? g_ascii_string_to_signed(copy, 10, G_MININT64, 0,
                                                &negative, NULL) &&
                           bosonoga_set_whole(&parsed, op->made, negative)
                     : g_ascii_string_to_unsigned(copy, 10, 0, G_MAXUINT64,
                                                  &positive, NULL) &&
                           bosonoga_set_whole(&parsed, op->made, positive);
    if (is_parsed) {
      *number = parsed;
    }
  }
  bosonoga_give_bool(execution, 0, is_parsed);
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
          return bosonoga_fail_at_op(op, error);
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
          return bosonoga_fail_at_op(op, error);
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
          return bosonoga_fail_at_op(op, error);
        }
        break;
      case BOSONOGA_OP_ACCUMULATOR_STORE:
        if (!bosonoga_accumulator_store(accumulator, &frame[op->slot], error)) {
          return bosonoga_fail_at_op(op, error);
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
      case BOSONOGA_OP_AWAIT_NEXT_FRAME:
        return BOSONOGA_EXECUTION_AWAITING_FRAME;
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
          bosonoga_runtime_error(op, error,
                                 "the function '%.*s' ended without a return",
                                 (gint)function->name_length, function->name);
        } else {
          bosonoga_runtime_error(op, error,
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
          bosonoga_runtime_error(op, error,
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
            .font = bosonoga_operand_value(&op->third, frame)->asset.index,
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
      case BOSONOGA_OP_DRAW_ICON: {
        const BosonogaGfxShape icon = {
            .kind = BOSONOGA_GFX_ICON,
            .x = left->f32,
            .y = right->f32,
            .icon = bosonoga_operand_value(&op->third, frame)->asset.index,
            .angle = bosonoga_operand_value(&op->fourth, frame)->f32,
            .color = bosonoga_operand_value(&op->fifth, frame)->color,
        };
        g_array_append_val(scene->shapes, icon);
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
        if (!bosonoga_read_array(execution, op, left, frame, error)) {
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_ARRAY_INIT:
        bosonoga_init_array(program, op, frame);
        break;
      case BOSONOGA_OP_ARRAY_WRITE:
        if (!bosonoga_write_array(execution, op, left, frame, error)) {
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_NEW_STR_PART:
        bosonoga_print_value(execution->join, left);
        if (execution->join->len > BOSONOGA_NEW_STR_LENGTH_MAX) {
          bosonoga_runtime_error(op, error,
                                 "new str of is longer than %d bytes",
                                 BOSONOGA_NEW_STR_LENGTH_MAX);
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_NEW_STR: {
        GString *join = execution->join;
        frame[op->slot].kind = op->made;
        frame[op->slot].str.text =
            bosonoga_own(execution, join->str, join->len);
        frame[op->slot].str.length = join->len;
        g_string_truncate(join, 0);
        break;
      }
      case BOSONOGA_OP_UNSET:
        bosonoga_free_owned(execution, frame[op->slot].str.text);
        memset(&frame[op->slot], 0, sizeof(frame[op->slot]));
        break;
      case BOSONOGA_OP_XML_PART:
        if (!bosonoga_xml_part(execution, op, left, error)) {
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_JOIN_STORE:
        bosonoga_join_store(execution, op, frame);
        break;
      case BOSONOGA_OP_IO_READ:
        if (!bosonoga_io_read_op(execution, op, frame, error)) {
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_IO_WRITE:
        if (!bosonoga_io_write_op(execution, op, frame, error)) {
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_IO_MODIFIED:
        if (!bosonoga_io_modified_op(execution, op, frame, error)) {
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_XML_CHILD:
      case BOSONOGA_OP_XML_ATTRIBUTE:
        if (!bosonoga_xml_read_op(execution, op, frame, error)) {
          return BOSONOGA_EXECUTION_FAILED;
        }
        break;
      case BOSONOGA_OP_STR_PARSE:
        bosonoga_str_parse_op(execution, op, frame);
        break;
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
