#include "gfx.h"

#define SOKOL_IMPL
#define SOKOL_GLCORE
#define SOKOL_NO_ENTRY
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>

#include "daria.h"
#include "interpreter.h"

// A frame runs at most this many ops of gfx_main, so that a long loop in it
// does not stop the window from responding.
#define DARIA_GFX_OPS_PER_FRAME 100000U

typedef struct {
  const DariaProgram *program;
  DariaExecution *main_execution;
  gint *exit_status;
  GError *error;         // The first error, which sets ended.
  gboolean initialized;  // Whether gfx_init finished.
  gboolean ended;        // Whether gfx_main returned or there was an error.
} DariaGfxRun;

static gboolean daria_gfx_run_callback(DariaGfxRun *run, DariaSection section) {
  g_autoptr(GError) error = NULL;
  if (daria_run_section(run->program, section, NULL, run->exit_status,
                        &error)) {
    return TRUE;
  }
  if (run->error == NULL) {
    run->error = g_steal_pointer(&error);
  }
  run->ended = TRUE;
  sapp_quit();
  return FALSE;
}

static void daria_gfx_init(void *user_data) {
  DariaGfxRun *run = user_data;
  sg_setup(&(sg_desc){
      .environment = sglue_environment(),
      .logger.func = slog_func,
  });
  run->initialized = daria_gfx_run_callback(run, DARIA_SECTION_GFX_INIT);
}

static void daria_gfx_frame(void *user_data) {
  DariaGfxRun *run = user_data;
  if (run->ended) {
    return;
  }

  sg_begin_pass(&(sg_pass){
      .action.colors[0] =
          {
              .load_action = SG_LOADACTION_CLEAR,
              .clear_value = {0.1f, 0.1f, 0.1f, 1.0f},
          },
      .swapchain = sglue_swapchain(),
  });
  const gboolean drawn = daria_gfx_run_callback(run, DARIA_SECTION_GFX_FRAME);
  sg_end_pass();
  sg_commit();
  if (!drawn) {
    return;
  }

  switch (daria_execution_advance(run->main_execution, DARIA_GFX_OPS_PER_FRAME,
                                  run->exit_status, &run->error)) {
    case DARIA_EXECUTION_RUNNING:
    case DARIA_EXECUTION_SLEEPING:
      return;
    case DARIA_EXECUTION_RETURNED:
    case DARIA_EXECUTION_FAILED:
      break;
  }
  run->ended = TRUE;
  sapp_quit();
}

static void daria_gfx_cleanup(void *user_data) {
  DariaGfxRun *run = user_data;
  if (!run->ended) {
    g_set_error(&run->error, DARIA_ERROR, DARIA_ERROR_RUNTIME,
                "%s: the window was closed before the program returned",
                run->program->path);
  }
  if (run->initialized) {
    daria_gfx_run_callback(run, DARIA_SECTION_GFX_CLEANUP);
  }
  sg_shutdown();
}

gboolean daria_gfx_run(const DariaProgram *program, gint width, gint height,
                       gint *exit_status, GError **error) {
  g_autoptr(DariaExecution) main_execution =
      daria_execution_new(program, DARIA_SECTION_MAIN, NULL);
  DariaGfxRun run = {
      .program = program,
      .main_execution = main_execution,
      .exit_status = exit_status,
  };
  sapp_run(&(sapp_desc){
      .user_data = &run,
      .init_userdata_cb = daria_gfx_init,
      .frame_userdata_cb = daria_gfx_frame,
      .cleanup_userdata_cb = daria_gfx_cleanup,
      .width = width,
      .height = height,
      .window_title = program->path,
      .logger.func = slog_func,
  });
  if (run.error != NULL) {
    g_propagate_error(error, run.error);
    return FALSE;
  }
  return TRUE;
}
