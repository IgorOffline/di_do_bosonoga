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
// A rectangle is drawn as two triangles.
#define DARIA_GFX_VERTICES_PER_RECTANGLE 6U
#define DARIA_GFX_COLOR_CHANNELS 4

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
  gfloat position[2];
  gfloat color[DARIA_GFX_COLOR_CHANNELS];
} DariaGfxGpuVertex;

typedef struct {
  const DariaProgram *program;
  const DariaSettings *settings;
  DariaExecution *main_execution;
  DariaGfxScene scene;
  sg_pipeline pipeline;
  sg_bindings bindings;
  gsize vertex_buffer_size;  // In bytes, 0 until a frame draws something.
  gint64 next_frame_time;    // In monotonic microseconds.
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

static void daria_gfx_create_pipeline(DariaGfxRun *run) {
  const sg_shader shader = sg_make_shader(&(sg_shader_desc){
      .vertex_func.source = daria_gfx_vertex_shader,
      .fragment_func.source = daria_gfx_fragment_shader,
      .label = "daria-shader",
  });
  run->pipeline = sg_make_pipeline(&(sg_pipeline_desc){
      .shader = shader,
      .layout.attrs =
          {
              [0].format = SG_VERTEXFORMAT_FLOAT2,
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
    daria_gfx_create_pipeline(run);
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

// Converts 0xRRGGBB to the opaque RGBA of the GPU.
static void daria_gfx_color_channels(
    guint32 color, gfloat channels[DARIA_GFX_COLOR_CHANNELS]) {
  channels[0] = (gfloat)((color >> 16) & 0xffU) / 255.0f;
  channels[1] = (gfloat)((color >> 8) & 0xffU) / 255.0f;
  channels[2] = (gfloat)(color & 0xffU) / 255.0f;
  channels[3] = 1.0f;
}

// Stores the corners of a rectangle in vertices, converted from pixels to the
// coordinates of the GPU, which go from -1 to 1 with y growing up.
static void daria_gfx_rectangle_vertices(
    const DariaGfxRectangle *rectangle,
    DariaGfxGpuVertex vertices[DARIA_GFX_VERTICES_PER_RECTANGLE]) {
  gfloat channels[DARIA_GFX_COLOR_CHANNELS];
  daria_gfx_color_channels(rectangle->color, channels);
  const gfloat left = 2.0f * rectangle->x / sapp_widthf() - 1.0f;
  const gfloat right =
      2.0f * (rectangle->x + rectangle->width) / sapp_widthf() - 1.0f;
  const gfloat top = 1.0f - 2.0f * rectangle->y / sapp_heightf();
  const gfloat bottom =
      1.0f - 2.0f * (rectangle->y + rectangle->height) / sapp_heightf();
  const gfloat corners[DARIA_GFX_VERTICES_PER_RECTANGLE][2] = {
      {left, top}, {right, top},    {right, bottom},
      {left, top}, {right, bottom}, {left, bottom},
  };
  for (guint i = 0; i < DARIA_GFX_VERTICES_PER_RECTANGLE; ++i) {
    memcpy(vertices[i].position, corners[i], sizeof(vertices[i].position));
    memcpy(vertices[i].color, channels, sizeof(vertices[i].color));
  }
}

// Writes the rectangles of the frame to the GPU, and makes the buffer larger if
// they do not fit.
static void daria_gfx_upload_rectangles(DariaGfxRun *run) {
  const GArray *rectangles = run->scene.rectangles;
  if (rectangles->len == 0) {
    return;
  }

  const gsize vertex_count =
      (gsize)rectangles->len * DARIA_GFX_VERTICES_PER_RECTANGLE;
  const gsize size = vertex_count * sizeof(DariaGfxGpuVertex);
  if (size > run->vertex_buffer_size) {
    sg_destroy_buffer(run->bindings.vertex_buffers[0]);
    run->bindings.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
        .size = size,
        .usage = {.vertex_buffer = true, .write_transient = true},
        .label = "daria-rectangles",
    });
    run->vertex_buffer_size = size;
  }

  g_autofree DariaGfxGpuVertex *vertices =
      g_new(DariaGfxGpuVertex, vertex_count);
  for (guint i = 0; i < rectangles->len; ++i) {
    daria_gfx_rectangle_vertices(
        &g_array_index(rectangles, DariaGfxRectangle, i),
        &vertices[(gsize)i * DARIA_GFX_VERTICES_PER_RECTANGLE]);
  }
  sg_write_buffer_transient(&(sg_write_buffer_desc){
      .src.data = {vertices, size},
      .dst.buffer = run->bindings.vertex_buffers[0],
  });
}

static void daria_gfx_render_scene(DariaGfxRun *run) {
  daria_gfx_upload_rectangles(run);
  const guint rectangle_count = run->scene.rectangles->len;
  gfloat clear[DARIA_GFX_COLOR_CHANNELS];
  daria_gfx_color_channels(run->scene.clear_color, clear);
  sg_begin_pass(&(sg_pass){
      .action.colors[0] =
          {
              .load_action = SG_LOADACTION_CLEAR,
              .clear_value = {clear[0], clear[1], clear[2], clear[3]},
          },
      .swapchain = sglue_swapchain(),
  });
  if (rectangle_count > 0) {
    sg_apply_pipeline(run->pipeline);
    sg_apply_bindings(&run->bindings);
    sg_draw(0, (int)(rectangle_count * DARIA_GFX_VERTICES_PER_RECTANGLE), 1);
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

  run->scene.clear_color = run->settings->background_color;
  g_array_set_size(run->scene.rectangles, 0);
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
  g_autoptr(GArray) rectangles =
      g_array_new(FALSE, FALSE, sizeof(DariaGfxRectangle));
  DariaGfxRun run = {
      .program = program,
      .settings = settings,
      .main_execution = main_execution,
      .scene.rectangles = rectangles,
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
