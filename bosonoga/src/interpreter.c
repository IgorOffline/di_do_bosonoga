#include "bosonoga/interpreter.h"

#include "execution.h"

// So that the stack has memory, which a frame of no values points into.
#define BOSONOGA_STACK_RESERVED 256

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
  execution->stack = g_array_sized_new(FALSE, TRUE, sizeof(BosonogaValue),
                                       BOSONOGA_STACK_RESERVED);
  execution->calls = g_array_new(FALSE, FALSE, sizeof(BosonogaCall));
  execution->join = g_string_new(NULL);
  execution->owned = g_ptr_array_new_with_free_func(g_free);
  return execution;
}

void bosonoga_execution_free(BosonogaExecution *execution) {
  if (execution->to_stdout) {
    g_string_free(execution->output, TRUE);
  }
  g_array_unref(execution->stack);
  g_array_unref(execution->calls);
  g_string_free(execution->join, TRUE);
  g_ptr_array_unref(execution->owned);
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
  if (program->is_gfx) {
    g_set_error_literal(error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
                        "the console runner requires bosonoga_main");
    return FALSE;
  }
  g_autofree BosonogaValue *slots = bosonoga_slots_new(program);
  if (!bosonoga_run_section(program, BOSONOGA_SECTION_GLOBAL, slots, NULL,
                            output, exit_status, error)) {
    return FALSE;
  }
  return bosonoga_run_section(program, BOSONOGA_SECTION_MAIN, slots, NULL,
                              output, exit_status, error);
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
