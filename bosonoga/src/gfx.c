#include "bosonoga/gfx.h"

#include <math.h>
#include <string.h>

#define SOKOL_GLCORE
#define SOKOL_NO_ENTRY
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_gl.h>
#include <sokol_glue.h>
#include <sokol_log.h>

// sokol_fontstash.h needs sokol_gfx.h, sokol_gl.h and fontstash.h before it.
#include <fontstash.h>
#include <sokol_fontstash.h>

#include "bosonoga/bosonoga.h"
#include "bosonoga/interpreter.h"

// A frame runs at most this many ops of gfx_main, so that a long loop in it
// does not stop the window from responding.
#define BOSONOGA_GFX_OPS_PER_FRAME 100000U
#define BOSONOGA_GFX_FRAME_MICROSECONDS (G_USEC_PER_SEC / 60)
#define BOSONOGA_GFX_HEX_CORNERS 6U
// Enough that a circle of corela's size looks round.
#define BOSONOGA_GFX_CIRCLE_CORNERS 48U
// Of shapes and glyphs in one frame, which sokol_gl holds on the CPU and GPU.
#define BOSONOGA_GFX_VERTICES_MAX (1 << 18)

typedef struct {
  const BosonogaProgram *program;
  const BosonogaSettings *settings;
  BosonogaValue *slots;
  BosonogaExecution *main_execution;
  BosonogaGfxScene scene;
  FONScontext *fons;
  GArray *font_ids;
  GPtrArray *font_data;  // Which fontstash reads from, and does not free.
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

static guint32 bosonoga_gfx_big_endian(const guchar *bytes, guint count) {
  guint32 value = 0;
  for (guint i = 0; i < count; ++i) {
    value = value << 8 | bytes[i];
  }
  return value;
}

// stb_truetype crashes on a file that is not a font, so the tables that it
// reads first must be in the file: cmap, head, hhea and hmtx.
static gboolean bosonoga_gfx_is_truetype(const guchar *data, gsize length) {
  static const struct {
    const gchar *tag;
    guint32 size;
  } required[] = {{"cmap", 4}, {"head", 54}, {"hhea", 36}, {"hmtx", 4}};
  if (length < 12) {
    return FALSE;
  }
  const guint32 version = bosonoga_gfx_big_endian(data, 4);
  if (version != 0x00010000U && version != 0x74727565U /* true */ &&
      version != 0x4F54544FU /* OTTO */) {
    return FALSE;
  }
  const guint32 table_count = bosonoga_gfx_big_endian(data + 4, 2);
  if (12 + 16 * (gsize)table_count > length) {
    return FALSE;
  }
  guint found = 0;
  for (guint32 i = 0; i < table_count; ++i) {
    const guchar *entry = data + 12 + 16 * i;
    const guint64 offset = bosonoga_gfx_big_endian(entry + 8, 4);
    const guint64 size = bosonoga_gfx_big_endian(entry + 12, 4);
    if (offset + size > length) {
      return FALSE;
    }
    for (guint j = 0; j < G_N_ELEMENTS(required); ++j) {
      if (memcmp(entry, required[j].tag, 4) == 0) {
        if (size < required[j].size) {
          return FALSE;
        }
        found |= 1U << j;
      }
    }
  }
  return found == (1U << G_N_ELEMENTS(required)) - 1;
}

static gboolean bosonoga_gfx_load_font(BosonogaGfxRun *run,
                                       const BosonogaProgramFont *font) {
  gchar *data = NULL;
  gsize length = 0;
  GError *read_error = NULL;
  if (!g_file_get_contents(font->path, &data, &length, &read_error)) {
    g_propagate_prefixed_error(&run->error, read_error,
                               "%s: font_%s: ", run->program->path, font->key);
    return FALSE;
  }
  g_ptr_array_add(run->font_data, data);
  const int id = length > G_MAXINT ||
                         !bosonoga_gfx_is_truetype((const guchar *)data, length)
                     ? FONS_INVALID
                     : fonsAddFontMem(run->fons, font->key,
                                      (unsigned char *)data, (int)length, 0);
  if (id == FONS_INVALID) {
    g_set_error(&run->error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
                "%s: font_%s: %s is not a TrueType font", run->program->path,
                font->key, font->path);
    return FALSE;
  }
  g_array_append_val(run->font_ids, id);
  return TRUE;
}

static void bosonoga_gfx_init(void *user_data) {
  BosonogaGfxRun *run = user_data;
  sg_setup(&(sg_desc){
      .environment = sglue_environment(),
      .logger.func = slog_func,
  });
  // Its pipelines must match the swapchain, which has no depth buffer.
  sgl_setup(&(sgl_desc_t){
      .max_vertices = BOSONOGA_GFX_VERTICES_MAX,
      .depth_format = SG_PIXELFORMAT_NONE,
      .logger.func = slog_func,
  });
  run->fons = sfons_create(&(sfons_desc_t){0});
  for (guint i = 0; i < run->program->fonts->len; ++i) {
    if (!bosonoga_gfx_load_font(
            run, &g_array_index(run->program->fonts, BosonogaProgramFont, i))) {
      bosonoga_gfx_fail(run);
      return;
    }
  }
  run->initialized = bosonoga_gfx_run_callback(run, BOSONOGA_SECTION_GLOBAL) &&
                     bosonoga_gfx_run_callback(run, BOSONOGA_SECTION_GFX_INIT);
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

static guint8 bosonoga_gfx_channel(guint32 color, guint shift) {
  return (guint8)((color >> shift) & 0xffU);
}

static void bosonoga_gfx_draw_rectangle(const BosonogaGfxShape *rectangle) {
  const gfloat left = rectangle->x;
  const gfloat top = rectangle->y;
  const gfloat right = rectangle->x + rectangle->width;
  const gfloat bottom = rectangle->y + rectangle->height;
  sgl_begin_triangles();
  sgl_c3b(bosonoga_gfx_channel(rectangle->color, 16),
          bosonoga_gfx_channel(rectangle->color, 8),
          bosonoga_gfx_channel(rectangle->color, 0));
  sgl_v2f(left, top);
  sgl_v2f(right, top);
  sgl_v2f(right, bottom);
  sgl_v2f(left, top);
  sgl_v2f(right, bottom);
  sgl_v2f(left, bottom);
  sgl_end();
}

static void bosonoga_gfx_draw_fan(const BosonogaGfxShape *shape,
                                  guint corner_count) {
  const gfloat step = (gfloat)(2.0 * G_PI) / (gfloat)corner_count;
  sgl_begin_triangles();
  sgl_c3b(bosonoga_gfx_channel(shape->color, 16),
          bosonoga_gfx_channel(shape->color, 8),
          bosonoga_gfx_channel(shape->color, 0));
  for (guint i = 0; i < corner_count; ++i) {
    const gfloat start = (gfloat)i * step;
    const gfloat end = (gfloat)(i + 1) * step;
    sgl_v2f(shape->x, shape->y);
    sgl_v2f(shape->x + shape->radius * cosf(start),
            shape->y + shape->radius * sinf(start));
    sgl_v2f(shape->x + shape->radius * cosf(end),
            shape->y + shape->radius * sinf(end));
  }
  sgl_end();
}

static int bosonoga_gfx_fons_align(guint32 align) {
  static const struct {
    BosonogaGfxAlign align;
    int fons;
  } aligns[] = {
      {BOSONOGA_GFX_ALIGN_LEFT, FONS_ALIGN_LEFT},
      {BOSONOGA_GFX_ALIGN_CENTER, FONS_ALIGN_CENTER},
      {BOSONOGA_GFX_ALIGN_RIGHT, FONS_ALIGN_RIGHT},
      {BOSONOGA_GFX_ALIGN_TOP, FONS_ALIGN_TOP},
      {BOSONOGA_GFX_ALIGN_MIDDLE, FONS_ALIGN_MIDDLE},
      {BOSONOGA_GFX_ALIGN_BOTTOM, FONS_ALIGN_BOTTOM},
      {BOSONOGA_GFX_ALIGN_BASELINE, FONS_ALIGN_BASELINE},
  };
  int fons = 0;
  for (gsize i = 0; i < G_N_ELEMENTS(aligns); ++i) {
    if ((align & (guint32)aligns[i].align) != 0) {
      fons |= aligns[i].fons;
    }
  }
  return fons;
}

static void bosonoga_gfx_draw_text(BosonogaGfxRun *run,
                                   const BosonogaGfxShape *text) {
  FONScontext *fons = run->fons;
  fonsSetFont(fons, g_array_index(run->font_ids, int, text->font));
  fonsSetSize(fons, text->size);
  fonsSetColor(fons, sfons_rgba(bosonoga_gfx_channel(text->color, 16),
                                bosonoga_gfx_channel(text->color, 8),
                                bosonoga_gfx_channel(text->color, 0), 0xff));
  fonsSetAlign(fons, bosonoga_gfx_fons_align(text->align));
  const gchar *start = run->scene.text->str + text->text;
  fonsDrawText(fons, text->x, text->y, start, start + text->text_length);
}

// Shapes and texts are one list of sokol_gl, so that each is drawn over the
// ones before it.
static gboolean bosonoga_gfx_record_scene(BosonogaGfxRun *run) {
  sgl_defaults();
  sgl_matrix_mode_projection();
  sgl_ortho(0.0f, sapp_widthf(), sapp_heightf(), 0.0f, -1.0f, 1.0f);
  const GArray *shapes = run->scene.shapes;
  for (guint i = 0; i < shapes->len; ++i) {
    const BosonogaGfxShape *shape = &g_array_index(shapes, BosonogaGfxShape, i);
    switch (shape->kind) {
      case BOSONOGA_GFX_RECTANGLE:
        bosonoga_gfx_draw_rectangle(shape);
        break;
      case BOSONOGA_GFX_HEX:
        bosonoga_gfx_draw_fan(shape, BOSONOGA_GFX_HEX_CORNERS);
        break;
      case BOSONOGA_GFX_CIRCLE:
        bosonoga_gfx_draw_fan(shape, BOSONOGA_GFX_CIRCLE_CORNERS);
        break;
      case BOSONOGA_GFX_TEXT:
        bosonoga_gfx_draw_text(run, shape);
        break;
    }
  }
  sfons_flush(run->fons);
  // A full sokol_gl drops what does not fit, which would look like a frame
  // that works.
  const sgl_error_t error = sgl_error();
  if (error.any) {
    g_set_error(&run->error, BOSONOGA_ERROR, BOSONOGA_ERROR_RUNTIME,
                "%s: gfx_frame draws more than %d vertices or commands",
                run->program->path, BOSONOGA_GFX_VERTICES_MAX);
    bosonoga_gfx_fail(run);
    return FALSE;
  }
  return TRUE;
}

static void bosonoga_gfx_render_scene(BosonogaGfxRun *run) {
  const guint32 clear = run->scene.clear_color;
  sg_begin_pass(&(sg_pass){
      .action.colors[0] =
          {
              .load_action = SG_LOADACTION_CLEAR,
              .clear_value =
                  {
                      (gfloat)bosonoga_gfx_channel(clear, 16) / 255.0f,
                      (gfloat)bosonoga_gfx_channel(clear, 8) / 255.0f,
                      (gfloat)bosonoga_gfx_channel(clear, 0) / 255.0f,
                      1.0f,
                  },
          },
      .swapchain = sglue_swapchain(),
  });
  sgl_draw();
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
  g_string_truncate(run->scene.text, 0);
  if (!bosonoga_gfx_run_callback(run, BOSONOGA_SECTION_GFX_FRAME) ||
      !bosonoga_gfx_record_scene(run)) {
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
  sfons_destroy(run->fons);
  sgl_shutdown();
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
  g_autoptr(GString) text = g_string_new(NULL);
  g_autoptr(GArray) font_ids = g_array_new(FALSE, FALSE, sizeof(int));
  g_autoptr(GPtrArray) font_data = g_ptr_array_new_with_free_func(g_free);
  BosonogaGfxRun run = {
      .program = program,
      .settings = settings,
      .slots = slots,
      .main_execution = main_execution,
      .scene.shapes = shapes,
      .scene.text = text,
      .font_ids = font_ids,
      .font_data = font_data,
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
