#include "bosonoga/gfx.h"

#define SOKOL_IMPL
#define SOKOL_GLCORE
#define SOKOL_NO_ENTRY
#include <math.h>
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <string.h>

#include "bosonoga/bosonoga.h"
#include "bosonoga/interpreter.h"

// A frame runs at most this many ops of gfx_main, so that a long loop in it
// does not stop the window from responding.
#define BOSONOGA_GFX_OPS_PER_FRAME 100000U
#define BOSONOGA_GFX_FRAME_MICROSECONDS (G_USEC_PER_SEC / 60)
#define BOSONOGA_GFX_VERTICES_PER_RECTANGLE 6U
#define BOSONOGA_GFX_HEX_CORNERS 6U
// Enough that a circle of corela's size looks round.
#define BOSONOGA_GFX_CIRCLE_CORNERS 48U
#define BOSONOGA_GFX_COLOR_CHANNELS 4

static const gchar bosonoga_gfx_vertex_shader[] =
    "#version 330\n"
    "layout(location = 0) in vec4 position;\n"
    "layout(location = 1) in vec4 color0;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "  gl_Position = position;\n"
    "  color = color0;\n"
    "}\n";

static const gchar bosonoga_gfx_fragment_shader[] =
    "#version 330\n"
    "in vec4 color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "  frag_color = color;\n"
    "}\n";

typedef struct {
  gfloat position[2];
  gfloat color[BOSONOGA_GFX_COLOR_CHANNELS];
} BosonogaGfxGpuVertex;

typedef struct {
  const BosonogaProgram *program;
  const BosonogaSettings *settings;
  BosonogaValue *slots;
  BosonogaExecution *main_execution;
  BosonogaGfxScene scene;
  sg_pipeline pipeline;
  sg_bindings bindings;
  gsize vertex_buffer_size;
  gint64 next_frame_time;
  gint64 last_frame_start;
  gint *exit_status;
  GError *error;  // The first error, which sets ended.
  gboolean initialized;
  gboolean ended;
} BosonogaGfxRun;

static void bosonoga_gfx_fail(BosonogaGfxRun *run) {
  run->ended = TRUE;
  sapp_quit();
}

static gboolean bosonoga_gfx_run_callback(BosonogaGfxRun *run,
                                          BosonogaSection section) {
  g_autoptr(GError) error = NULL;
  if (bosonoga_run_section(run->program, section, run->slots, &run->scene, NULL,
                           run->exit_status, &error)) {
    return TRUE;
  }
  if (run->error == NULL) {
    run->error = g_steal_pointer(&error);
  }
  bosonoga_gfx_fail(run);
  return FALSE;
}

static void bosonoga_gfx_measure_delta_time(BosonogaGfxRun *run) {
  const gint64 now = g_get_monotonic_time();
  const gint64 elapsed = run->last_frame_start == 0
                             ? BOSONOGA_GFX_FRAME_MICROSECONDS
                             : now - run->last_frame_start;
  run->scene.delta_time = (gfloat)elapsed / (gfloat)G_USEC_PER_SEC;
  run->last_frame_start = now;
}

static void bosonoga_gfx_create_pipeline(BosonogaGfxRun *run) {
  const sg_shader shader = sg_make_shader(&(sg_shader_desc){
      .vertex_func.source = bosonoga_gfx_vertex_shader,
      .fragment_func.source = bosonoga_gfx_fragment_shader,
      .label = "bosonoga-shader",
  });
  run->pipeline = sg_make_pipeline(&(sg_pipeline_desc){
      .shader = shader,
      .layout.attrs =
          {
              [0].format = SG_VERTEXFORMAT_FLOAT2,
              [1].format = SG_VERTEXFORMAT_FLOAT4,
          },
      .label = "bosonoga-pipeline",
  });
  // Drawing with a failed pipeline does nothing, which would look like a
  // window that works. Sokol has logged the reason.
  if (sg_query_pipeline_state(run->pipeline) != SG_RESOURCESTATE_VALID) {
    g_set_error(&run->error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
                "%s: could not create the gfx pipeline", run->program->path);
    bosonoga_gfx_fail(run);
  }
}

static void bosonoga_gfx_init(void *user_data) {
  BosonogaGfxRun *run = user_data;
  sg_setup(&(sg_desc){
      .environment = sglue_environment(),
      .logger.func = slog_func,
  });
  run->initialized = bosonoga_gfx_run_callback(run, BOSONOGA_SECTION_GLOBAL) &&
                     bosonoga_gfx_run_callback(run, BOSONOGA_SECTION_GFX_INIT);
  if (run->initialized) {
    bosonoga_gfx_create_pipeline(run);
  }
}

// Only holds back a display faster than 60 Hz. Each wait is measured from the
// previous frame time, so that errors do not add up.
static void bosonoga_gfx_wait_for_frame_time(BosonogaGfxRun *run) {
  const gint64 now = g_get_monotonic_time();
  if (now < run->next_frame_time) {
    g_usleep((gulong)(run->next_frame_time - now));
    run->next_frame_time += BOSONOGA_GFX_FRAME_MICROSECONDS;
  } else {
    run->next_frame_time = now + BOSONOGA_GFX_FRAME_MICROSECONDS;
  }
}

static void bosonoga_gfx_color_channels(
    guint32 color, gfloat channels[BOSONOGA_GFX_COLOR_CHANNELS]) {
  channels[0] = (gfloat)((color >> 16) & 0xffU) / 255.0f;
  channels[1] = (gfloat)((color >> 8) & 0xffU) / 255.0f;
  channels[2] = (gfloat)(color & 0xffU) / 255.0f;
  channels[3] = 1.0f;
}

// GPU coordinates go from -1 to 1, with y growing up.
static void bosonoga_gfx_vertex(
    gfloat x, gfloat y, const gfloat channels[BOSONOGA_GFX_COLOR_CHANNELS],
    BosonogaGfxGpuVertex *vertex) {
  vertex->position[0] = 2.0f * x / sapp_widthf() - 1.0f;
  vertex->position[1] = 1.0f - 2.0f * y / sapp_heightf();
  memcpy(vertex->color, channels, sizeof(vertex->color));
}

static guint bosonoga_gfx_vertex_count(const BosonogaGfxShape *shape) {
  switch (shape->kind) {
    case BOSONOGA_GFX_RECTANGLE:
      return BOSONOGA_GFX_VERTICES_PER_RECTANGLE;
    case BOSONOGA_GFX_HEX:
      return 3U * BOSONOGA_GFX_HEX_CORNERS;
    case BOSONOGA_GFX_CIRCLE:
      return 3U * BOSONOGA_GFX_CIRCLE_CORNERS;
  }
  g_return_val_if_reached(0);
}

static void bosonoga_gfx_rectangle_vertices(
    const BosonogaGfxShape *rectangle,
    const gfloat channels[BOSONOGA_GFX_COLOR_CHANNELS],
    BosonogaGfxGpuVertex vertices[BOSONOGA_GFX_VERTICES_PER_RECTANGLE]) {
  const gfloat left = rectangle->x;
  const gfloat top = rectangle->y;
  const gfloat right = rectangle->x + rectangle->width;
  const gfloat bottom = rectangle->y + rectangle->height;
  const gfloat corners[BOSONOGA_GFX_VERTICES_PER_RECTANGLE][2] = {
      {left, top}, {right, top},    {right, bottom},
      {left, top}, {right, bottom}, {left, bottom},
  };
  for (guint i = 0; i < BOSONOGA_GFX_VERTICES_PER_RECTANGLE; ++i) {
    bosonoga_gfx_vertex(corners[i][0], corners[i][1], channels, &vertices[i]);
  }
}

// A fan of one triangle per side of a regular polygon, around its center.
// Corner i is at i of corner_count parts of a turn, so that the first is
// straight right, as in corela.
static void bosonoga_gfx_fan_vertices(
    const BosonogaGfxShape *shape, guint corner_count,
    const gfloat channels[BOSONOGA_GFX_COLOR_CHANNELS],
    BosonogaGfxGpuVertex *vertices) {
  const gfloat step = (gfloat)(2.0 * G_PI) / (gfloat)corner_count;
  for (guint i = 0; i < corner_count; ++i) {
    const gfloat start = (gfloat)i * step;
    const gfloat end = (gfloat)(i + 1) * step;
    BosonogaGfxGpuVertex *triangle = &vertices[3 * i];
    bosonoga_gfx_vertex(shape->x, shape->y, channels, &triangle[0]);
    bosonoga_gfx_vertex(shape->x + shape->radius * cosf(start),
                        shape->y + shape->radius * sinf(start), channels,
                        &triangle[1]);
    bosonoga_gfx_vertex(shape->x + shape->radius * cosf(end),
                        shape->y + shape->radius * sinf(end), channels,
                        &triangle[2]);
  }
}

// The number of vertices to draw.
static gsize bosonoga_gfx_upload_shapes(BosonogaGfxRun *run) {
  const GArray *shapes = run->scene.shapes;
  gsize vertex_count = 0;
  for (guint i = 0; i < shapes->len; ++i) {
    vertex_count +=
        bosonoga_gfx_vertex_count(&g_array_index(shapes, BosonogaGfxShape, i));
  }
  if (vertex_count == 0) {
    return 0;
  }

  const gsize size = vertex_count * sizeof(BosonogaGfxGpuVertex);
  if (size > run->vertex_buffer_size) {
    sg_destroy_buffer(run->bindings.vertex_buffers[0]);
    run->bindings.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
        .size = size,
        .usage = {.vertex_buffer = true, .write_transient = true},
        .label = "bosonoga-shapes",
    });
    run->vertex_buffer_size = size;
  }

  g_autofree BosonogaGfxGpuVertex *vertices =
      g_new(BosonogaGfxGpuVertex, vertex_count);
  BosonogaGfxGpuVertex *next = vertices;
  for (guint i = 0; i < shapes->len; ++i) {
    const BosonogaGfxShape *shape = &g_array_index(shapes, BosonogaGfxShape, i);
    gfloat channels[BOSONOGA_GFX_COLOR_CHANNELS];
    bosonoga_gfx_color_channels(shape->color, channels);
    switch (shape->kind) {
      case BOSONOGA_GFX_RECTANGLE:
        bosonoga_gfx_rectangle_vertices(shape, channels, next);
        break;
      case BOSONOGA_GFX_HEX:
        bosonoga_gfx_fan_vertices(shape, BOSONOGA_GFX_HEX_CORNERS, channels,
                                  next);
        break;
      case BOSONOGA_GFX_CIRCLE:
        bosonoga_gfx_fan_vertices(shape, BOSONOGA_GFX_CIRCLE_CORNERS, channels,
                                  next);
        break;
    }
    next += bosonoga_gfx_vertex_count(shape);
  }
  sg_write_buffer_transient(&(sg_write_buffer_desc){
      .src.data = {vertices, size},
      .dst.buffer = run->bindings.vertex_buffers[0],
  });
  return vertex_count;
}

static void bosonoga_gfx_render_scene(BosonogaGfxRun *run) {
  const gsize vertex_count = bosonoga_gfx_upload_shapes(run);
  gfloat clear[BOSONOGA_GFX_COLOR_CHANNELS];
  bosonoga_gfx_color_channels(run->scene.clear_color, clear);
  sg_begin_pass(&(sg_pass){
      .action.colors[0] =
          {
              .load_action = SG_LOADACTION_CLEAR,
              .clear_value = {clear[0], clear[1], clear[2], clear[3]},
          },
      .swapchain = sglue_swapchain(),
  });
  if (vertex_count > 0) {
    sg_apply_pipeline(run->pipeline);
    sg_apply_bindings(&run->bindings);
    sg_draw(0, (int)vertex_count, 1);
  }
  sg_end_pass();
  sg_commit();
}

static void bosonoga_gfx_frame(void *user_data) {
  BosonogaGfxRun *run = user_data;
  if (run->ended) {
    return;
  }
  bosonoga_gfx_wait_for_frame_time(run);
  bosonoga_gfx_measure_delta_time(run);

  run->scene.clear_color = run->settings->background_color;
  g_array_set_size(run->scene.shapes, 0);
  if (!bosonoga_gfx_run_callback(run, BOSONOGA_SECTION_GFX_FRAME)) {
    return;
  }
  bosonoga_gfx_render_scene(run);

  switch (bosonoga_execution_advance(run->main_execution,
                                     BOSONOGA_GFX_OPS_PER_FRAME,
                                     run->exit_status, &run->error)) {
    case BOSONOGA_EXECUTION_RUNNING:
    case BOSONOGA_EXECUTION_SLEEPING:
      return;
    case BOSONOGA_EXECUTION_RETURNED:
    case BOSONOGA_EXECUTION_FAILED:
      break;
  }
  run->ended = TRUE;
  sapp_quit();
}

// As in corela, a key held down runs gfx_key_down once,
// not again as it repeats.
static void bosonoga_gfx_event(const sapp_event *event, void *user_data) {
  BosonogaGfxRun *run = user_data;
  if (event->type != SAPP_EVENTTYPE_KEY_DOWN || event->key_repeat ||
      !run->initialized || run->ended) {
    return;
  }
  run->scene.key = (guint32)event->key_code;
  bosonoga_gfx_run_callback(run, BOSONOGA_SECTION_GFX_KEY_DOWN);
}

static void bosonoga_gfx_cleanup(void *user_data) {
  BosonogaGfxRun *run = user_data;
  if (!run->ended) {
    g_set_error(&run->error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
                "%s: the window was closed before the program returned",
                run->program->path);
  }
  if (run->initialized) {
    bosonoga_gfx_run_callback(run, BOSONOGA_SECTION_GFX_CLEANUP);
  }
  sg_shutdown();
}

gboolean bosonoga_gfx_run(const BosonogaProgram *program,
                          const BosonogaSettings *settings, gint *exit_status,
                          GError **error) {
  g_autofree BosonogaValue *slots = bosonoga_slots_new(program);
  g_autoptr(BosonogaExecution) main_execution =
      bosonoga_execution_new(program, BOSONOGA_SECTION_MAIN, slots, NULL, NULL);
  g_autoptr(GArray) shapes =
      g_array_new(FALSE, FALSE, sizeof(BosonogaGfxShape));
  BosonogaGfxRun run = {
      .program = program,
      .settings = settings,
      .slots = slots,
      .main_execution = main_execution,
      .scene.shapes = shapes,
      .exit_status = exit_status,
  };
  sapp_run(&(sapp_desc){
      .user_data = &run,
      .init_userdata_cb = bosonoga_gfx_init,
      .frame_userdata_cb = bosonoga_gfx_frame,
      .cleanup_userdata_cb = bosonoga_gfx_cleanup,
      .event_userdata_cb = bosonoga_gfx_event,
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
