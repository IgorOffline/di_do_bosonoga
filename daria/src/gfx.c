#include "gfx.h"

#define SOKOL_IMPL
#define SOKOL_GLCORE
#define SOKOL_NO_ENTRY
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <string.h>

#include "daria.h"
#include "interpreter.h"

// A frame runs at most this many ops of gfx_main, so that a long loop in it
// does not stop the window from responding.
#define DARIA_GFX_OPS_PER_FRAME 100000U
#define DARIA_GFX_FRAME_MICROSECONDS (G_USEC_PER_SEC / 60)

static const gchar daria_gfx_vertex_shader[] =
    "#version 330\n"
    "layout(location = 0) in vec4 position;\n"
    "layout(location = 1) in vec4 color0;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "  gl_Position = position;\n"
    "  color = color0;\n"
    "}\n";

static const gchar daria_gfx_fragment_shader[] =
    "#version 330\n"
    "in vec4 color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "  frag_color = color;\n"
    "}\n";

// A vertex as the GPU reads it, in the order of the attributes.
typedef struct {
  gfloat position[3];
  gfloat color[DARIA_COLOR_CHANNELS];
} DariaGfxGpuVertex;

typedef struct {
  const DariaProgram *program;
  const DariaSettings *settings;
  DariaExecution *main_execution;
  DariaGfxScene scene;
  sg_pipeline pipeline;
  sg_bindings bindings;
  gint64 next_frame_time;  // In monotonic microseconds.
  gint *exit_status;
  GError *error;         // The first error, which sets ended.
  gboolean initialized;  // Whether gfx_init finished.
  gboolean ended;        // Whether gfx_main returned or there was an error.
} DariaGfxRun;

// Ends the run with an error that is already in run->error.
static void daria_gfx_fail(DariaGfxRun *run) {
  run->ended = TRUE;
  sapp_quit();
}

static gboolean daria_gfx_run_callback(DariaGfxRun *run, DariaSection section) {
  g_autoptr(GError) error = NULL;
  if (daria_run_section(run->program, section, &run->scene, NULL,
                        run->exit_status, &error)) {
    return TRUE;
  }
  if (run->error == NULL) {
    run->error = g_steal_pointer(&error);
  }
  daria_gfx_fail(run);
  return FALSE;
}

// Puts the vertices from gfx_init on the GPU, with their colors looked up in
// the settings, and creates what draws them.
static void daria_gfx_create_resources(DariaGfxRun *run) {
  const GArray *vertices = run->scene.vertices;
  if (vertices->len == 0) {
    return;
  }

  g_autofree DariaGfxGpuVertex *gpu_vertices =
      g_new(DariaGfxGpuVertex, vertices->len);
  for (guint i = 0; i < vertices->len; ++i) {
    const DariaGfxVertex *vertex = &g_array_index(vertices, DariaGfxVertex, i);
    memcpy(gpu_vertices[i].position, vertex->position,
           sizeof(gpu_vertices[i].position));
    memcpy(gpu_vertices[i].color, run->settings->colors[vertex->color],
           sizeof(gpu_vertices[i].color));
  }
  run->bindings.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
      .data = {gpu_vertices, vertices->len * sizeof(*gpu_vertices)},
      .label = "daria-vertices",
  });

  const sg_shader shader = sg_make_shader(&(sg_shader_desc){
      .vertex_func.source = daria_gfx_vertex_shader,
      .fragment_func.source = daria_gfx_fragment_shader,
      .label = "daria-shader",
  });
  run->pipeline = sg_make_pipeline(&(sg_pipeline_desc){
      .shader = shader,
      .layout.attrs =
          {
              [0].format = SG_VERTEXFORMAT_FLOAT3,
              [1].format = SG_VERTEXFORMAT_FLOAT4,
          },
      .label = "daria-pipeline",
  });
  // Drawing with a failed pipeline does nothing, which would look like a
  // window that works. Sokol has logged the reason.
  if (sg_query_pipeline_state(run->pipeline) != SG_RESOURCESTATE_VALID) {
    g_set_error(&run->error, DARIA_ERROR, DARIA_ERROR_RUNTIME,
                "%s: could not create the gfx pipeline", run->program->path);
    daria_gfx_fail(run);
  }
}

static void daria_gfx_init(void *user_data) {
  DariaGfxRun *run = user_data;
  sg_setup(&(sg_desc){
      .environment = sglue_environment(),
      .logger.func = slog_func,
  });
  run->initialized = daria_gfx_run_callback(run, DARIA_SECTION_GFX_INIT);
  if (run->initialized) {
    daria_gfx_create_resources(run);
  }
}

// The window waits for the display after each frame, which keeps a 60 Hz
// display at 60 frames per second, so this only holds back a faster display.
// Each wait is measured from the previous frame time, so its inaccuracy does
// not add up.
static void daria_gfx_wait_for_frame_time(DariaGfxRun *run) {
  const gint64 now = g_get_monotonic_time();
  if (now < run->next_frame_time) {
    g_usleep((gulong)(run->next_frame_time - now));
    run->next_frame_time += DARIA_GFX_FRAME_MICROSECONDS;
  } else {
    // A late frame does not make the next ones hurry to catch up.
    run->next_frame_time = now + DARIA_GFX_FRAME_MICROSECONDS;
  }
}

static void daria_gfx_render_scene(DariaGfxRun *run) {
  const gfloat *clear = run->settings->colors[run->scene.clear_color];
  sg_begin_pass(&(sg_pass){
      .action.colors[0] =
          {
              .load_action = SG_LOADACTION_CLEAR,
              .clear_value = {clear[0], clear[1], clear[2], clear[3]},
          },
      .swapchain = sglue_swapchain(),
  });
  if (run->scene.draw && run->scene.vertices->len > 0) {
    sg_apply_pipeline(run->pipeline);
    sg_apply_bindings(&run->bindings);
    sg_draw(0, (int)run->scene.vertices->len, 1);
  }
  sg_end_pass();
  sg_commit();
}

static void daria_gfx_frame(void *user_data) {
  DariaGfxRun *run = user_data;
  if (run->ended) {
    return;
  }
  daria_gfx_wait_for_frame_time(run);

  run->scene.clear_color = DARIA_COLOR_BACKGROUND;
  run->scene.draw = FALSE;
  if (!daria_gfx_run_callback(run, DARIA_SECTION_GFX_FRAME)) {
    return;
  }
  daria_gfx_render_scene(run);

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

gboolean daria_gfx_run(const DariaProgram *program,
                       const DariaSettings *settings, gint *exit_status,
                       GError **error) {
  g_autoptr(DariaExecution) main_execution =
      daria_execution_new(program, DARIA_SECTION_MAIN, NULL, NULL);
  g_autoptr(GArray) vertices =
      g_array_new(FALSE, FALSE, sizeof(DariaGfxVertex));
  DariaGfxRun run = {
      .program = program,
      .settings = settings,
      .main_execution = main_execution,
      .scene.vertices = vertices,
      .exit_status = exit_status,
  };
  sapp_run(&(sapp_desc){
      .user_data = &run,
      .init_userdata_cb = daria_gfx_init,
      .frame_userdata_cb = daria_gfx_frame,
      .cleanup_userdata_cb = daria_gfx_cleanup,
      .width = settings->gfx_width,
      .height = settings->gfx_height,
      .depth_format = SAPP_PIXELFORMAT_NONE,
      .gl = {.major_version = 3, .minor_version = 3},
      .window_title = program->path,
      .logger.func = slog_func,
  });
  if (run.error != NULL) {
    g_propagate_error(error, run.error);
    return FALSE;
  }
  return TRUE;
}
