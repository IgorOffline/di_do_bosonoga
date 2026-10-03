#include "core.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

#include "regina.h"

#if defined(SOKOL_VULKAN)
#include <vulkan/vulkan_core.h>
#endif
// clang-format off
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_nuklear.h"
#include "gfx.h"
#include "gfx.glsl.h"
// clang-format on
#define PROGRAM_MAX_BYTES (BOSONO_LIMIT - 1)
#define WINDOW_WIDTH 1152
#define WINDOW_HEIGHT 648
#define RECT_X 546.0f
#define RECT_Y 304.0f
#define RECT_WIDTH 60.0f
#define RECT_HEIGHT 40.0f
#define RECT_SPACING 120.0f
#define RECT_COLOR_COUNT BOSONO_THEME_COLOR_COUNT
#define CAMERA_BATCH_COUNT 4
#define CAMERA_START_BATCH 0
#define CAMERA_BATCH_WIDTH 1100.0f
#define CAMERA_RENDER_HALF_WIDTH 1100.0f
#define LINE_COUNT 32
#define NUMBER_COUNT 32
#define TOKEN_COUNT 32

#if defined(SOKOL_VULKAN)
#define printf_vulkan_metadata()                                             \
  do {                                                                       \
    sapp_environment const environment = sapp_get_environment();             \
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;                       \
    _Static_assert(                                                          \
        sizeof physical_device == sizeof environment.vulkan.physical_device, \
        "Vulkan device handle size must match Sokol's handle");              \
    memcpy(&physical_device, &environment.vulkan.physical_device,            \
           sizeof physical_device);                                          \
    if (physical_device != VK_NULL_HANDLE) {                                 \
      VkPhysicalDeviceProperties properties = {0};                           \
      VkPhysicalDeviceMemoryProperties memory = {0};                         \
      vkGetPhysicalDeviceProperties(physical_device, &properties);           \
      vkGetPhysicalDeviceMemoryProperties(physical_device, &memory);         \
      printf(                                                                \
          "Vulkan device: %s\n"                                              \
          "Device API version: %u.%u.%u (variant %u)\n"                      \
          "Vendor ID: 0x%04X; device ID: 0x%04X\n"                           \
          "Driver version (vendor encoding): 0x%08X\n"                       \
          "Device type: %u; graphics queue family: %u\n",                    \
          properties.deviceName,                                             \
          (unsigned int)VK_API_VERSION_MAJOR(properties.apiVersion),         \
          (unsigned int)VK_API_VERSION_MINOR(properties.apiVersion),         \
          (unsigned int)VK_API_VERSION_PATCH(properties.apiVersion),         \
          (unsigned int)VK_API_VERSION_VARIANT(properties.apiVersion),       \
          (unsigned int)properties.vendorID,                                 \
          (unsigned int)properties.deviceID,                                 \
          (unsigned int)properties.driverVersion,                            \
          (unsigned int)properties.deviceType,                               \
          (unsigned int)environment.vulkan.queue_family_index);              \
      for (uint32_t heap = 0; heap < memory.memoryHeapCount; heap++) {       \
        printf("Memory heap %u: %llu bytes%s\n", (unsigned int)heap,         \
               (unsigned long long)memory.memoryHeaps[heap].size,            \
               (memory.memoryHeaps[heap].flags &                             \
                VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0                        \
                   ? " (device-local)"                                       \
                   : "");                                                    \
      }                                                                      \
    } else {                                                                 \
      fputs("Vulkan metadata unavailable: no physical device\n", stderr);    \
    }                                                                        \
    (void)fflush(stdout);                                                    \
  } while (0)
#else
#define printf_vulkan_metadata() \
  ((void)puts("Graphics backend: Metal; Vulkan metadata does not apply"))
#endif

typedef struct {
  _Alignas(16) size_t bytes;
} CountedBlock;

static void* counted_alloc(size_t size, void* user_data) {
  Regina* const regina = user_data;
  if (size > SIZE_MAX - sizeof(CountedBlock)) return NULL;
  CountedBlock* const block = malloc(sizeof *block + size);
  if (!block) return NULL;
  block->bytes = sizeof *block + size;
  regina->malloc_bytes += block->bytes;
  return block + 1;
}

static void counted_free(void* pointer, void* user_data) {
  Regina* const regina = user_data;
  if (!pointer) return;
  CountedBlock* const block = (CountedBlock*)pointer - 1;
  regina->malloc_bytes -= block->bytes;
  free(block);
}

static void* counted_nk_alloc(nk_handle handle, void* old, nk_size size) {
  (void)old;
  return counted_alloc((size_t)size, handle.ptr);
}

static void counted_nk_free(nk_handle handle, void* pointer) {
  counted_free(pointer, handle.ptr);
}

static void label_fps(Regina* regina, double fps) {
  (void)snprintf(regina->fps_label, sizeof regina->fps_label,
                 "FPS: %.0f MB: %.1f", fps,
                 (double)regina->malloc_bytes / (1024.0 * 1024.0));
}

static double frame_clock(void) {
#if defined(_WIN32)
  LARGE_INTEGER counter, frequency;
  QueryPerformanceCounter(&counter);
  QueryPerformanceFrequency(&frequency);
  return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
#endif
}

static void pace_frame(Regina* regina) {
  double now = frame_clock();
  while (now < regina->next_frame) {
    double remaining = regina->next_frame - now;
#if defined(_WIN32)
    if (remaining > 0.002)
      Sleep((DWORD)((remaining - 0.001) * 1000.0));
    else
      SwitchToThread();
#else
    struct timespec delay = {0, (long)(remaining * 1000000000.0)};
    nanosleep(&delay, NULL);
#endif
    now = frame_clock();
  }
  regina->next_frame = now + 1.0 / 60.0;
}

static void core_event(sapp_event const* event, void* user_data) {
  Regina* const regina = user_data;
  if (event->type == SAPP_EVENTTYPE_KEY_DOWN ||
      event->type == SAPP_EVENTTYPE_KEY_UP) {
    bool const down = event->type == SAPP_EVENTTYPE_KEY_DOWN;
    if (event->key_code == SAPP_KEYCODE_A) regina->camera_left = down;
    if (event->key_code == SAPP_KEYCODE_D) regina->camera_right = down;
    for (size_t rule = 0; rule < regina->script.rule_count; rule++) {
      char const trigger = regina->script.rules[rule].key;
      sapp_keycode const key =
          (sapp_keycode)(trigger >= '0' && trigger <= '9'
                             ? SAPP_KEYCODE_0 + trigger - '0'
                             : SAPP_KEYCODE_A + trigger - 'a');
      if (key == event->key_code) {
        if (down && !event->key_repeat && !regina->rule_held[rule] &&
            regina->script.rules[rule].trigger == BOSONO_TRIGGER_PRESS) {
          if (regina->script.rules[rule].start) {
            regina->started = true;
            regina->camera_x = 0.0f;
            regina->camera_dirty = true;
            regina->camera_left = false;
            regina->camera_right = false;
          }
          for (size_t line = 0; line < regina->script.rules[rule].info_count;
               line++) {
            (void)printf("%s\n", regina->script.rules[rule].info[line]);
          }
          if (regina->script.rules[rule].info_count != 0) (void)fflush(stdout);
        }
        regina->rule_held[rule] = down;
      }
    }
  } else if (event->type == SAPP_EVENTTYPE_UNFOCUSED) {
    regina->camera_left = false;
    regina->camera_right = false;
    for (size_t rule = 0; rule < regina->script.rule_count; rule++)
      regina->rule_held[rule] = false;
  }
  if (event->type == SAPP_EVENTTYPE_KEY_DOWN && !event->key_repeat &&
      event->key_code >= SAPP_KEYCODE_1 && event->key_code <= SAPP_KEYCODE_4) {
    size_t const requested = (size_t)(event->key_code - SAPP_KEYCODE_1);
    if (requested < regina->script.theme_count &&
        requested != regina->selected_theme) {
      regina->selected_theme = requested;
      regina->theme_dirty = true;
    }
  }
}

static void core_cleanup(void* user_data) {
  Regina* const regina = user_data;
  regina->script.rule_count = 0;
  regina->script.theme_count = 0;
  memset(regina->rule_held, 0, sizeof regina->rule_held);
  if (regina->text_ready) snk_shutdown();
  if (regina->font_atlas.temporary.alloc)
    nk_font_atlas_clear(&regina->font_atlas);
  sg_shutdown();
}

static void core_frame(void* user_data) {
  Regina* const regina = user_data;
  if (regina->graphics_failed) {
    sapp_quit();
    return;
  }
  pace_frame(regina);
  double const frame_time = frame_clock();
  if (regina->fps_frames == 0) regina->fps_started_at = frame_time;
  regina->fps_frames++;
  if (regina->fps_frames == 24) {
    double const elapsed = frame_time - regina->fps_started_at;
    if (elapsed > 0.0) label_fps(regina, 23.0 / elapsed);
    regina->fps_frames = 0;
  }
  bool printed = false;
  for (size_t rule = 0; rule < regina->script.rule_count; rule++) {
    if (!regina->rule_held[rule] ||
        regina->script.rules[rule].trigger != BOSONO_TRIGGER_DOWN)
      continue;
    if (regina->script.rules[rule].start) {
      regina->started = true;
      regina->camera_x = 0.0f;
      regina->camera_dirty = true;
      regina->camera_left = false;
      regina->camera_right = false;
    }
    for (size_t line = 0; line < regina->script.rules[rule].info_count;
         line++) {
      (void)printf("%s\n", regina->script.rules[rule].info[line]);
      printed = true;
    }
  }
  if (printed) (void)fflush(stdout);
  if (regina->started && regina->camera_left != regina->camera_right) {
    float elapsed = (float)sapp_frame_duration();
    if (elapsed > 0.1f) elapsed = 0.1f;
    regina->camera_x += (regina->camera_right ? 960.0f : -960.0f) * elapsed;
    float const minimum_x = 0.0f;
    float const maximum_x =
        (float)(CAMERA_BATCH_COUNT - CAMERA_START_BATCH) * CAMERA_BATCH_WIDTH;
    if (regina->camera_x < minimum_x) regina->camera_x = minimum_x;
    if (regina->camera_x > maximum_x) regina->camera_x = maximum_x;
    regina->camera_dirty = true;
  }
  bool const vertices_dirty = regina->theme_dirty || regina->camera_dirty;
  if (regina->theme_dirty) {
    BosonoTheme const* const theme =
        &regina->script.themes[regina->selected_theme];
    for (size_t channel = 0; channel < 3; channel++) {
      unsigned int const shift = 16U - 8U * (unsigned int)channel;
      regina->color_background[channel] =
          (float)((theme->background >> shift) & 255U) / 255.0f;
    }
    regina->pass_action.colors[0].clear_value =
        (sg_color){regina->color_background[0], regina->color_background[1],
                   regina->color_background[2], 1.0f};
    for (size_t world = 0; world < WORLD_COUNT; world++) {
      for (size_t corner = 0; corner < RECT_VERTEX_COUNT; corner++) {
        for (size_t channel = 0; channel < 3; channel++) {
          unsigned int const shift = 16U - 8U * (unsigned int)channel;
          regina->worlds[world].vertices[corner].color[channel] =
              (float)((theme->rectangles[world % RECT_COLOR_COUNT] >> shift) &
                      255U) /
              255.0f;
        }
      }
    }
    char title[BOSONO_THEME_NAME_LIMIT + 32];
    (void)snprintf(title, sizeof title, "di_do_bosonoga - %s", theme->name);
    sapp_set_window_title(title);
    regina->theme_dirty = false;
  }
  if (vertices_dirty) {
    for (size_t world = 0; world < WORLD_COUNT; world++) {
      float const x = RECT_X + (float)world * RECT_SPACING - regina->camera_x;
      for (size_t corner = 0; corner < RECT_VERTEX_COUNT; corner++) {
        bool const right = corner == 1 || corner == 2 || corner == 4;
        float const screen_x = x + (right ? RECT_WIDTH : 0.0f);
        regina->worlds[world].vertices[corner].position[0] =
            2.0f * screen_x / (float)WINDOW_WIDTH - 1.0f;
      }
      sg_update_buffer(
          regina->worlds[world].bindings.vertex_buffers[0],
          &(sg_range){.ptr = regina->worlds[world].vertices,
                      .size = sizeof regina->worlds[world].vertices});
    }
    regina->camera_dirty = false;
  }
  sg_begin_pass(&(sg_pass){.action = regina->pass_action,
                           .swapchain = sglue_swapchain()});
  int current_batch = (int)((regina->camera_x +
                             (float)CAMERA_START_BATCH * CAMERA_BATCH_WIDTH) /
                            CAMERA_BATCH_WIDTH);
  if (current_batch >= CAMERA_BATCH_COUNT)
    current_batch = CAMERA_BATCH_COUNT - 1;
  int const first_batch = current_batch > 0 ? current_batch - 1 : 0;
  int const last_batch = current_batch + 1 < CAMERA_BATCH_COUNT
                             ? current_batch + 1
                             : CAMERA_BATCH_COUNT - 1;
  float const render_left =
      (float)(first_batch - CAMERA_START_BATCH) * CAMERA_BATCH_WIDTH -
      CAMERA_RENDER_HALF_WIDTH;
  float const render_right =
      (float)(last_batch - CAMERA_START_BATCH) * CAMERA_BATCH_WIDTH +
      CAMERA_RENDER_HALF_WIDTH;
  unsigned int triangle_batches = 0;
  for (size_t world = 0; world < WORLD_COUNT; world++) {
    if (!regina->started) break;
    float const world_x = RECT_X + (float)world * RECT_SPACING;
    if (world_x + RECT_WIDTH <= render_left || world_x >= render_right)
      continue;
    sg_apply_pipeline(regina->worlds[world].pipeline);
    sg_apply_bindings(&regina->worlds[world].bindings);
    sg_draw(0, RECT_VERTEX_COUNT, 1);
    triangle_batches++;
  }
  char label[96];
  if (regina->started) {
    (void)snprintf(label, sizeof label, "CB: %d RB: %u CX: (%.1f)",
                   current_batch, triangle_batches, (double)regina->camera_x);
  } else {
    (void)snprintf(label, sizeof label, "Start?");
  }
  struct nk_context* ctx = snk_new_frame();
  if (nk_begin(ctx, "batch-count", nk_rect(8, 8, 640, 44),
               NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
    nk_draw_text(nk_window_get_canvas(ctx), nk_rect(12, 10, 620, 32), label,
                 (int)strlen(label), &regina->label_font->handle,
                 nk_rgba(0, 0, 0, 0), nk_rgb(255, 255, 255));
  }
  nk_end(ctx);
  float const fps_width = regina->label_font->handle.width(
      regina->label_font->handle.userdata, regina->label_font->handle.height,
      regina->fps_label, (int)strlen(regina->fps_label));
  float const fps_left = (float)sapp_width() - fps_width - 12.0f;
  if (nk_begin(ctx, "fps", nk_rect(fps_left - 4.0f, 8, fps_width + 8.0f, 44),
               NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
    nk_draw_text(nk_window_get_canvas(ctx),
                 nk_rect(fps_left, 10, fps_width, 32), regina->fps_label,
                 (int)strlen(regina->fps_label), &regina->label_font->handle,
                 nk_rgba(0, 0, 0, 0), nk_rgb(255, 255, 255));
  }
  nk_end(ctx);
  snk_render(sapp_width(), sapp_height());
  sg_end_pass();
  sg_commit();
}

static void core_init(void* user_data) {
  Regina* const regina = user_data;
  printf_vulkan_metadata();
  sg_setup(&(sg_desc){.environment = sglue_environment(),
                      .allocator = {.alloc_fn = counted_alloc,
                                    .free_fn = counted_free,
                                    .user_data = regina},
                      .logger.func = slog_func});
  if (!sg_isvalid()) {
    regina->graphics_failed = true;
    sapp_quit();
    return;
  }
  snk_setup(&(snk_desc_t){.no_default_font = true,
                          .max_vertices = 1024,
                          .dpi_scale = sapp_dpi_scale(),
                          .allocator = {.alloc_fn = counted_alloc,
                                        .free_fn = counted_free,
                                        .user_data = regina},
                          .logger.func = slog_func});
  regina->text_ready = true;
  nk_font_atlas_init(&regina->font_atlas,
                     &(struct nk_allocator){.userdata = nk_handle_ptr(regina),
                                            .alloc = counted_nk_alloc,
                                            .free = counted_nk_free});
  nk_font_atlas_begin(&regina->font_atlas);
  regina->label_font = nk_font_atlas_add_from_file(
      &regina->font_atlas, "asset/IndieFlower.ttf", 28.0f, NULL);
  if (!regina->label_font) {
    fputs("Unable to load asset/IndieFlower.ttf\n", stderr);
    regina->graphics_failed = true;
    sapp_quit();
    return;
  }
  int font_width, font_height;
  void const* pixels = nk_font_atlas_bake(&regina->font_atlas, &font_width,
                                          &font_height, NK_FONT_ATLAS_RGBA32);
  if (!pixels) {
    regina->graphics_failed = true;
    sapp_quit();
    return;
  }
  sg_image font_image = sg_make_image(&(sg_image_desc){
      .width = font_width,
      .height = font_height,
      .pixel_format = SG_PIXELFORMAT_RGBA8,
      .data.mip_levels[0] = {.ptr = pixels,
                             .size =
                                 (size_t)font_width * (size_t)font_height * 4U},
      .label = "batch-label-font"});
  sg_view font_view =
      sg_make_view(&(sg_view_desc){.texture.image = font_image});
  snk_image_t font_handle =
      snk_make_image(&(snk_image_desc_t){.texture_view = font_view});
  nk_font_atlas_end(&regina->font_atlas, snk_nkhandle(font_handle), NULL);
  nk_font_atlas_cleanup(&regina->font_atlas);
  struct nk_context* ctx = snk_new_frame();
  nk_style_set_font(ctx, &regina->label_font->handle);
  ctx->style.window.fixed_background = nk_style_item_color(nk_rgba(0, 0, 0, 0));
  if (sg_query_image_state(font_image) != SG_RESOURCESTATE_VALID ||
      sg_query_view_state(font_view) != SG_RESOURCESTATE_VALID) {
    regina->graphics_failed = true;
  }
  char const* const buffer_labels[RECT_COLOR_COUNT] = {
      "red-world-vertices", "green-world-vertices", "blue-world-vertices"};
  char const* const pipeline_labels[RECT_COLOR_COUNT] = {
      "red-world-pipeline", "green-world-pipeline", "blue-world-pipeline"};
  for (size_t world = 0; world < WORLD_COUNT; world++) {
    World* const current = &regina->worlds[world];
    current->bindings.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
        .size = sizeof current->vertices,
        .usage = {.vertex_buffer = true, .dynamic_update = true},
        .label = buffer_labels[world % RECT_COLOR_COUNT]});
    current->shader = GFX_MAKE_SHADER();
    current->pipeline = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = current->shader,
        .layout.attrs = {[GFX_ATTR_POSITION].format = SG_VERTEXFORMAT_FLOAT3,
                         [GFX_ATTR_COLOR].format = SG_VERTEXFORMAT_FLOAT4},
        .label = pipeline_labels[world % RECT_COLOR_COUNT]});
    if (sg_query_buffer_state(current->bindings.vertex_buffers[0]) !=
            SG_RESOURCESTATE_VALID ||
        sg_query_shader_state(current->shader) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(current->pipeline) != SG_RESOURCESTATE_VALID) {
      regina->graphics_failed = true;
    }
  }
  regina->pass_action = (sg_pass_action){
      .colors[0] = {.load_action = SG_LOADACTION_CLEAR,
                    .clear_value = {regina->color_background[0],
                                    regina->color_background[1],
                                    regina->color_background[2], 1.0f}}};
}

int core(Regina* regina) {
  memset(regina, 0, offsetof(Regina, script));
  regina->script.rule_count = 0;
  regina->malloc_bytes = REGINA_BYTES;
  regina->theme_dirty = true;
  label_fps(regina, 0.0);

  FILE* program_file = NULL;
#if defined(_WIN32)
  int const program_error = fopen_s(&program_file, "main.bosonoga", "rb");
#else
  program_file = fopen("main.bosonoga", "rb");
  int const program_error = program_file ? 0 : errno;
#endif
  if (!program_file) {
    fprintf(stderr, "Unable to open main.bosonoga (error %d)\n", program_error);
    return EXIT_FAILURE;
  }
  size_t const program_length =
      fread(regina->source, 1, PROGRAM_MAX_BYTES, program_file);
  int const extra_char = fgetc(program_file);
  if (ferror(program_file)) {
    fprintf(stderr, "Unable to read main.bosonoga\n");
    (void)fclose(program_file);
    return EXIT_FAILURE;
  }
  (void)fclose(program_file);
  if (extra_char != EOF) {
    fprintf(stderr, "main.bosonoga exceeds %d bytes\n", PROGRAM_MAX_BYTES);
    return EXIT_FAILURE;
  }
  regina->source[program_length] = '\0';
  Tokens tokens = {.items = regina->tokens};
  if (tokenize(regina->source, &tokens) != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }
  if (parse(&tokens, &regina->script) != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }
  if (regina->script.theme_count == 0) {
    fputs("main.bosonoga must declare at least one theme\n", stderr);
    return EXIT_FAILURE;
  }

  uint32_t const* const colors =
      regina->script.themes[regina->selected_theme].rectangles;
  for (size_t channel = 0; channel < 3; channel++) {
    unsigned int const shift = 16U - 8U * (unsigned int)channel;
    regina->color_background[channel] =
        (float)((regina->script.themes[regina->selected_theme].background >>
                 shift) &
                255U) /
        255.0f;
  }
  for (size_t world = 0; world < WORLD_COUNT; world++) {
    float const x = RECT_X + (float)world * RECT_SPACING;
    float const corners[RECT_VERTEX_COUNT][2] = {
        {x, RECT_Y},
        {x + RECT_WIDTH, RECT_Y},
        {x + RECT_WIDTH, RECT_Y + RECT_HEIGHT},
        {x, RECT_Y},
        {x + RECT_WIDTH, RECT_Y + RECT_HEIGHT},
        {x, RECT_Y + RECT_HEIGHT},
    };
    float color_rect[3];
    for (size_t channel = 0; channel < 3; channel++) {
      unsigned int const shift = 16U - 8U * (unsigned int)channel;
      color_rect[channel] =
          (float)((colors[world % RECT_COLOR_COUNT] >> shift) & 255U) / 255.0f;
    }
    for (size_t corner = 0; corner < RECT_VERTEX_COUNT; corner++) {
      regina->worlds[world].vertices[corner] = (Vertex){
          .position = {2.0f * corners[corner][0] / (float)WINDOW_WIDTH - 1.0f,
                       1.0f - 2.0f * corners[corner][1] / (float)WINDOW_HEIGHT,
                       0.0f},
          .color = {color_rect[0], color_rect[1], color_rect[2], 1.0f}};
    }
  }
  sapp_run(&(sapp_desc){.user_data = regina,
                        .init_userdata_cb = core_init,
                        .frame_userdata_cb = core_frame,
                        .cleanup_userdata_cb = core_cleanup,
                        .event_userdata_cb = core_event,
                        .width = WINDOW_WIDTH,
                        .height = WINDOW_HEIGHT,
                        .swap_interval = 0,
                        .depth_format = SAPP_PIXELFORMAT_NONE,
                        .window_title = "di_do_bosonoga",
                        .fixed_size = true,
                        .icon.sokol_default = true,
                        .allocator = {.alloc_fn = counted_alloc,
                                      .free_fn = counted_free,
                                      .user_data = regina},
                        .logger.func = slog_func});
  return regina->graphics_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
#define SOKOL_IMPL
// clang-format off
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
// clang-format on

#define NK_IMPLEMENTATION
#include "nuklear.h"
#include "sokol_nuklear.h"
