#include <string.h>

#include "bosonoga-private.h"

static gboolean evaluate_comparison(const Regina* regina, gsize position,
                                    gboolean* comparison, GError** error) {
  const gchar* operation = token_at(regina, position + 1);

  if (g_str_equal(operation, "shift_has")) {
    guint32 left;
    guint32 right;
    if (!bosonoga_parse_flags_at(regina, position, operation, &left, error) ||
        !bosonoga_parse_flags_at(regina, position + 2, operation, &right,
                                 error)) {
      return FALSE;
    }
    *comparison = (left & right) == right;
    return TRUE;
  }
  if (g_str_equal(operation, "shift_eq")) {
    return fail_at(regina, position + 1, error, BOSONOGA_ERROR_SYNTAX,
                   "shift_eq was renamed shift_has");
  }

  BosonogaValue left;
  BosonogaValue right;
  if (!bosonoga_parse_value_at(regina, position, &left, error) ||
      !bosonoga_parse_value_at(regina, position + 2, &right, error)) {
    return FALSE;
  }
  if (left.kind != right.kind) {
    return fail_at(regina, position + 1, error, BOSONOGA_ERROR_TYPE,
                   "cannot compare %s '%s' with %s '%s'",
                   bosonoga_kind_name(left.kind), token_at(regina, position),
                   bosonoga_kind_name(right.kind),
                   token_at(regina, position + 2));
  }
  if (left.kind == BOSONOGA_KIND_FLAGS) {
    return fail_at(regina, position + 1, error, BOSONOGA_ERROR_TYPE,
                   "flags can only be compared with shift_has");
  }

  if (left.kind == BOSONOGA_KIND_F32) {
    if (g_str_equal(operation, "gt")) {
      *comparison = left.f32 > right.f32;
    } else if (g_str_equal(operation, "lt")) {
      *comparison = left.f32 < right.f32;
    } else if (g_str_equal(operation, "eq_3")) {
      *comparison =
          bosonoga_f32_close(left.f32, right.f32, BOSONOGA_F32_TOLERANCE_3);
    } else if (g_str_equal(operation, "eq_6")) {
      *comparison =
          bosonoga_f32_close(left.f32, right.f32, BOSONOGA_F32_TOLERANCE_6);
    } else if (g_str_equal(operation, "not_eq_3")) {
      *comparison =
          !bosonoga_f32_close(left.f32, right.f32, BOSONOGA_F32_TOLERANCE_3);
    } else if (g_str_equal(operation, "not_eq_6")) {
      *comparison =
          !bosonoga_f32_close(left.f32, right.f32, BOSONOGA_F32_TOLERANCE_6);
    } else if (g_str_equal(operation, "eq") ||
               g_str_equal(operation, "not_eq")) {
      return fail_at(regina, position + 1, error, BOSONOGA_ERROR_TYPE,
                     "f32 has no exact %s; use %s_3 or %s_6", operation,
                     operation, operation);
    } else {
      return fail_at(regina, position + 1, error, BOSONOGA_ERROR_SYNTAX,
                     "unknown comparison '%s'", operation);
    }
    return TRUE;
  }

  if (g_str_equal(operation, "gt")) {
    *comparison = left.i32 > right.i32;
  } else if (g_str_equal(operation, "lt")) {
    *comparison = left.i32 < right.i32;
  } else if (g_str_equal(operation, "eq")) {
    *comparison = left.i32 == right.i32;
  } else if (g_str_equal(operation, "not_eq")) {
    *comparison = left.i32 != right.i32;
  } else if (g_str_has_prefix(operation, "eq_") ||
             g_str_has_prefix(operation, "not_eq_")) {
    return fail_at(regina, position + 1, error, BOSONOGA_ERROR_TYPE,
                   "%s only works on f32; i32 uses eq / not_eq", operation);
  } else {
    return fail_at(regina, position + 1, error, BOSONOGA_ERROR_SYNTAX,
                   "unknown comparison '%s'", operation);
  }
  return TRUE;
}

static gboolean find_block_end(const Regina* regina, gsize opening, gsize end,
                               gsize* closing, GError** error) {
  const gsize start = opening + 1;
  gsize depth = 1;
  for (gsize position = start; position < end; position++) {
    const gchar* token = token_at(regina, position);
    if (g_str_equal(token, "di")) {
      depth++;
    } else if (g_str_equal(token, "do")) {
      depth--;
      if (depth == 0) {
        if (position == start) {
          return fail_at(regina, opening, error, BOSONOGA_ERROR_SYNTAX,
                         "empty block; write nihil for a block that does "
                         "nothing");
        }
        *closing = position;
        return TRUE;
      }
    }
  }
  return fail_at(regina, opening, error, BOSONOGA_ERROR_SYNTAX,
                 "'di' has no matching 'do'");
}

static gsize position_after_main(const Regina* regina) {
  for (gsize position = 0; position < regina->token_count; position++) {
    if (g_str_equal(token_at(regina, position), "main")) {
      return position + 1;
    }
  }
  return regina->token_count;
}

static gboolean run_body(Regina* regina, gsize position, gsize end,
                         GError** error);

static gboolean evaluate_condition(const Regina* regina, gsize* position,
                                   gsize end, gboolean* result,
                                   GError** error) {
  gsize next = *position;
  gboolean condition = FALSE;
  gboolean and_group = TRUE;
  while (TRUE) {
    if (end - next < BOSONOGA_COMPARISON_TOKEN_COUNT) {
      return fail_at(regina, next, error, BOSONOGA_ERROR_SYNTAX,
                     "incomplete condition");
    }
    gboolean comparison = FALSE;
    if (!evaluate_comparison(regina, next, &comparison, error)) {
      return FALSE;
    }
    and_group = and_group && comparison;
    next += BOSONOGA_COMPARISON_TOKEN_COUNT;
    if (next == end) {
      return fail_at(regina, next, error, BOSONOGA_ERROR_SYNTAX,
                     "the condition must be followed by 'di'");
    }

    const gchar* connector = token_at(regina, next);
    if (g_str_equal(connector, "or")) {
      condition = condition || and_group;
      and_group = TRUE;
    } else if (!g_str_equal(connector, "and")) {
      break;
    }
    next++;
  }
  *result = condition || and_group;
  *position = next;
  return TRUE;
}

static gboolean run_if(Regina* regina, gsize* position, gsize end,
                       GError** error) {
  gsize next = *position + 1;
  gboolean has_condition = TRUE;
  gboolean chosen = FALSE;
  gsize chosen_start = 0;
  gsize chosen_closing = 0;
  while (TRUE) {
    gboolean condition = TRUE;
    if (has_condition) {
      if (next < end && g_str_equal(token_at(regina, next), "di")) {
        return fail_at(regina, next, error, BOSONOGA_ERROR_SYNTAX,
                       "missing condition before 'di'");
      }
      if (!evaluate_condition(regina, &next, end, &condition, error)) {
        return FALSE;
      }
    }
    if (next >= end || !g_str_equal(token_at(regina, next), "di")) {
      return fail_at(regina, next, error, BOSONOGA_ERROR_SYNTAX,
                     "expected 'di', found '%s'",
                     next < end ? token_at(regina, next) : "nothing");
    }
    gsize closing;
    if (!find_block_end(regina, next, end, &closing, error)) {
      return FALSE;
    }
    if (!chosen && condition) {
      chosen = TRUE;
      chosen_start = next + 1;
      chosen_closing = closing;
    }
    next = closing + 1;

    if (!has_condition || next == end ||
        !g_str_equal(token_at(regina, next), "else")) {
      break;
    }
    next++;
    has_condition = next < end && g_str_equal(token_at(regina, next), "if");
    if (has_condition) {
      next++;
    }
  }

  gboolean ok = TRUE;
  if (chosen) {
    const gsize saved_count = regina->variable_count;
    ok = run_body(regina, chosen_start, chosen_closing, error);
    regina->variable_count = saved_count;
  }
  *position = next;
  return ok;
}

static gboolean run_loop(Regina* regina, gsize* position, gsize end,
                         GError** error) {
  const gsize start = *position;
  const gchar* keyword = token_at(regina, start);
  const gchar* count_text = keyword + strlen(BOSONOGA_LOOP_PREFIX);
  gint32 limit;
  if (!g_ascii_isdigit(*count_text) ||
      !bosonoga_parse_i32(count_text, strlen(count_text), &limit)) {
    return fail_at(regina, start, error, BOSONOGA_ERROR_SYNTAX,
                   "'%s' is not a statement; loops are written to_N, "
                   "e.g. to_8",
                   keyword);
  }
  if (end - start < BOSONOGA_LOOP_MIN_TOKEN_COUNT) {
    return fail_at(regina, start, error, BOSONOGA_ERROR_SYNTAX,
                   "'%s' needs a di ... do block", keyword);
  }
  gsize opening = start + 1;
  const gchar* name = NULL;
  if (g_str_equal(token_at(regina, opening), "with")) {
    if (end - start < BOSONOGA_LOOP_WITH_MIN_TOKEN_COUNT) {
      return fail_at(regina, opening, error, BOSONOGA_ERROR_SYNTAX,
                     "'with' needs a counter name and a di ... do block");
    }
    name = token_at(regina, opening + 1);
    opening += 2;
  }
  if (!g_str_equal(token_at(regina, opening), "di")) {
    return fail_at(regina, opening, error, BOSONOGA_ERROR_SYNTAX,
                   "expected 'di', found '%s'", token_at(regina, opening));
  }
  gsize closing;
  if (!find_block_end(regina, opening, end, &closing, error)) {
    return FALSE;
  }

  const gsize saved_count = regina->variable_count;
  BosonogaVariable* counter = NULL;
  if (name != NULL) {
    if (bosonoga_is_statement_keyword(name) || g_str_equal(name, "with")) {
      return fail_at(regina, opening - 1, error, BOSONOGA_ERROR_NAME,
                     "'%s' cannot be a loop counter", name);
    }
    if (bosonoga_index_of_variable(regina, name) !=
        BOSONOGA_VARIABLE_NOT_FOUND) {
      return fail_at(regina, opening - 1, error, BOSONOGA_ERROR_NAME,
                     "variable '%s' already exists", name);
    }
    if (regina->variable_count == BOSONOGA_VARIABLE_LIMIT) {
      return fail_at(regina, opening - 1, error, BOSONOGA_ERROR_LIMIT,
                     "more than %" G_GSIZE_FORMAT " variables",
                     (gsize)BOSONOGA_VARIABLE_LIMIT);
    }
    counter = bosonoga_add_variable(regina, name);
    counter->value.kind = BOSONOGA_KIND_I32;
  }

  const gsize body_count = regina->variable_count;
  gboolean ok = TRUE;
  for (gint32 i = 0; i < limit && ok && !regina->returned; i++) {
    if (counter != NULL) {
      counter->value.i32 = i;
    }
    ok = run_body(regina, opening + 1, closing, error);
    regina->variable_count = body_count;
  }
  regina->variable_count = saved_count;
  *position = closing + 1;
  return ok;
}

static gboolean run_body(Regina* regina, gsize position, gsize end,
                         GError** error) {
  while (position < end) {
    const gchar* keyword = token_at(regina, position);
    gboolean ok;
    if (g_str_equal(keyword, "var")) {
      ok = bosonoga_run_var(regina, &position, error);
    } else if (g_str_equal(keyword, "set")) {
      ok = bosonoga_run_set(regina, &position, error);
    } else if (g_str_equal(keyword, "if")) {
      ok = run_if(regina, &position, end, error);
    } else if (g_str_has_prefix(keyword, BOSONOGA_LOOP_PREFIX)) {
      ok = run_loop(regina, &position, end, error);
    } else if (g_str_equal(keyword, "nihil")) {
      position++;
      ok = TRUE;
    } else if (g_str_equal(keyword, "return")) {
      if (position + 1 >= end ||
          !bosonoga_parse_return_status(token_at(regina, position + 1),
                                        &regina->exit_status)) {
        return fail_at(regina, position, error, BOSONOGA_ERROR_SYNTAX,
                       "'return' needs exit_success or exit_failure");
      }
      regina->returned = TRUE;
      return TRUE;
    } else if (g_str_equal(keyword, "else")) {
      return fail_at(regina, position, error, BOSONOGA_ERROR_SYNTAX,
                     "'else' without a matching 'if'");
    } else {
      return fail_at(regina, position, error, BOSONOGA_ERROR_SYNTAX,
                     "unknown statement '%s'", keyword);
    }
    if (!ok || regina->returned) {
      return ok;
    }
    if (position > end) {
      return fail_at(regina, end, error, BOSONOGA_ERROR_SYNTAX,
                     "a statement runs past the end of its block");
    }
  }
  return TRUE;
}

static gboolean run_main(Regina* regina, GError** error) {
  if (!run_body(regina, position_after_main(regina), regina->token_count,
                error)) {
    return FALSE;
  }
  if (!regina->returned) {
    return fail_at(regina, regina->token_count, error, BOSONOGA_ERROR_RUNTIME,
                   "the program ended without 'return'");
  }
  return TRUE;
}

static void log_regina(const Regina* regina) {
  g_debug("Regina: %" G_GSIZE_FORMAT " MiB, %" G_GSIZE_FORMAT
          " lines (%" G_GSIZE_FORMAT " header), %" G_GSIZE_FORMAT
          " tokens, %" G_GSIZE_FORMAT " variables",
          (gsize)(sizeof(Regina) / BOSONOGA_MIB), regina->line_count,
          regina->header_line_count, regina->token_count,
          regina->variable_count);
  for (gsize i = 0; i < regina->version_count; i++) {
    g_debug("version[%" G_GSIZE_FORMAT "]: %s", i, regina->version[i].string);
  }
  for (gsize i = 0; i < regina->lega_count; i++) {
    g_debug("lega[%" G_GSIZE_FORMAT "]: %s", i, regina->lega[i].legal);
  }
  for (gsize i = 0; i < regina->aka_count; i++) {
    g_debug("aka[%" G_GSIZE_FORMAT "]: %s -> %s", i, regina->aka[i].original,
            regina->aka[i].replacement);
  }
  for (gsize i = 0; i < regina->token_count; i++) {
    g_debug("tokens[%" G_GSIZE_FORMAT "]: %s", i, token_at(regina, i));
  }
  for (gsize i = 0; i < regina->variable_count; i++) {
    const BosonogaVariable* variable = &regina->variables[i];
    switch (variable->value.kind) {
      case BOSONOGA_KIND_FLAGS:
        g_debug("variables[%" G_GSIZE_FORMAT "]: %s = flags %" G_GUINT32_FORMAT,
                i, variable->name, variable->value.flags);
        break;
      case BOSONOGA_KIND_F32:
        g_debug("variables[%" G_GSIZE_FORMAT "]: %s = %.6g_f32", i,
                variable->name, (gdouble)variable->value.f32);
        break;
      case BOSONOGA_KIND_I32:
        g_debug("variables[%" G_GSIZE_FORMAT "]: %s = %" G_GINT32_FORMAT "_i32",
                i, variable->name, variable->value.i32);
        break;
    }
  }
}

static void reset_regina(Regina* regina) {
  regina->line_count = 0;
  regina->header_line_count = 0;
  regina->token_count = 0;
  regina->variable_count = 0;
  regina->version_count = 0;
  regina->lega_count = 0;
  regina->aka_count = 0;
  regina->main_count = 0;
  regina->returned = FALSE;
  regina->exit_status = BOSONOGA_EXIT_FAILURE;
}

gboolean bosonoga_core(Regina* regina, const gchar* header, const gchar* input,
                       gint* exit_status, GError** error) {
  g_return_val_if_fail(regina != NULL, FALSE);
  g_return_val_if_fail(header != NULL, FALSE);
  g_return_val_if_fail(input != NULL, FALSE);
  g_return_val_if_fail(exit_status != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  reset_regina(regina);
  const gboolean ok = bosonoga_load_program(regina, header, input, error) &&
                      run_main(regina, error);
  if (!g_log_writer_default_would_drop(G_LOG_LEVEL_DEBUG, G_LOG_DOMAIN)) {
    log_regina(regina);
  }
  if (ok) {
    *exit_status = regina->exit_status;
  }
  return ok;
}
