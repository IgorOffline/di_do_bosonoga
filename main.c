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

#include "bosono/parser.h"
#include "bosono/tokenizer.h"

#if defined(__APPLE__)
#define SOKOL_METAL
#else
#define SOKOL_VULKAN
#include <vulkan/vulkan_core.h>
#endif
#define SOKOL_NO_ENTRY
// clang-format off
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#include "nuklear.h"
#include "sokol_nuklear.h"
#include "gfx.h"
#include "gfx.glsl.h"
// clang-format on
#define PROGRAM_MAX_BYTES (BOSONO_LIMIT - 1)
#define RULE_COUNT 8
#define RULE_INFO_COUNT 4
#define WINDOW_WIDTH 1152
#define WINDOW_HEIGHT 648
#define RECT_X 546.0f
#define RECT_Y 304.0f
#define RECT_WIDTH 60.0f
#define RECT_HEIGHT 40.0f
#define RECT_SPACING 120.0f
#define WORLD_COUNT 30
#define RECT_COLOR_COUNT 3
#define CAMERA_BATCH_COUNT 4
#define CAMERA_START_BATCH 0
#define CAMERA_BATCH_WIDTH 1100.0f
#define CAMERA_RENDER_HALF_WIDTH 1100.0f
#define RECT_VERTEX_COUNT 6
#define LINE_COUNT 32
#define NUMBER_COUNT 32
#define TOKEN_COUNT 32
#define PHASE_START 0
#define PHASE_INIT 1
#define PHASE_FRAME 2
#define PHASE_CLEANUP 3
#define PHASE_EVENT 4
#define THEME_COUNT 4

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

static void pace_frame(void) {
  static double next_frame;
  double now = frame_clock();
  while (now < next_frame) {
    double remaining = next_frame - now;
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
  // Reset after a stall instead of issuing catch-up frames above the cap.
  next_frame = now + 1.0 / 60.0;
}

int main(void);
#define MAIN_CALLBACKS()                               \
  static int main_phase = PHASE_START;                 \
  static sapp_event main_event;                        \
  static void main_init(void) {                        \
    printf_vulkan_metadata();                          \
    main_phase = PHASE_INIT;                           \
    (void)main();                                      \
  }                                                    \
  static void main_frame(void) {                       \
    main_phase = PHASE_FRAME;                          \
    (void)main();                                      \
  }                                                    \
  static void main_on_event(sapp_event const* event) { \
    main_event = *event;                               \
    main_phase = PHASE_EVENT;                          \
    (void)main();                                      \
  }                                                    \
  static void main_cleanup(void) {                     \
    main_phase = PHASE_CLEANUP;                        \
    (void)main();                                      \
  }
MAIN_CALLBACKS()
#undef MAIN_CALLBACKS

int main(void) {
  typedef struct {
    float position[3];
    float color[4];
  } Vertex;
  // Each rectangle owns its geometry and GPU resources independently.
  typedef struct {
    Vertex vertices[RECT_VERTEX_COUNT];
    sg_shader shader;
    sg_pipeline pipeline;
    sg_bindings bindings;
  } World;
  static World worlds[WORLD_COUNT];
  static float color_background[3];
  static sg_pass_action pass_action;
  static bool graphics_failed;
  static bool text_ready;
  static struct nk_font_atlas font_atlas;
  static struct nk_font* label_font;
  typedef struct {
    char const* name;
    uint32_t rectangles[RECT_COLOR_COUNT];
    uint32_t background;
  } Theme;
  static Theme const themes[THEME_COUNT] = {
      {.name = "Latte",
       .rectangles = {0xD20F39U, 0x40A02BU, 0x1E66F5U},
       .background = 0xACB0BEU},
      {.name = "Frappe",
       .rectangles = {0xE78284U, 0xA6D189U, 0x8CAAEEU},
       .background = 0x626880U},
      {.name = "Macchiato",
       .rectangles = {0xED8796U, 0xA6DA95U, 0x8AADF4U},
       .background = 0x5B6078U},
      {.name = "Mocha",
       .rectangles = {0xF38BA8U, 0xA6E3A1U, 0x89B4FAU},
       .background = 0x585B70U},
  };
  static size_t selected_theme = 0;
  static bool theme_dirty = true;
  static float camera_x = 0.0f;
  static bool camera_dirty;
  static bool camera_left;
  static bool camera_right;
  // Stand-in for the parser output: held-key blocks containing info lines.
  typedef struct {
    sapp_keycode key;
    bool held;
    char const* info[RULE_INFO_COUNT];
    size_t info_count;
  } Rule;
  static Rule rules[RULE_COUNT];
  static size_t rule_count;

  if (main_phase == PHASE_EVENT) {
    if (main_event.type == SAPP_EVENTTYPE_KEY_DOWN ||
        main_event.type == SAPP_EVENTTYPE_KEY_UP) {
      bool const down = main_event.type == SAPP_EVENTTYPE_KEY_DOWN;
      if (main_event.key_code == SAPP_KEYCODE_A) camera_left = down;
      if (main_event.key_code == SAPP_KEYCODE_D) camera_right = down;
      for (size_t rule = 0; rule < rule_count; rule++)
        if (rules[rule].key == main_event.key_code) rules[rule].held = down;
    } else if (main_event.type == SAPP_EVENTTYPE_UNFOCUSED) {
      camera_left = false;
      camera_right = false;
      for (size_t rule = 0; rule < rule_count; rule++) rules[rule].held = false;
    }
    // Physical top-row keys have distinct codes from SAPP_KEYCODE_KP_1..4.
    if (main_event.type == SAPP_EVENTTYPE_KEY_DOWN && !main_event.key_repeat &&
        main_event.key_code >= SAPP_KEYCODE_1 &&
        main_event.key_code <= SAPP_KEYCODE_4) {
      size_t const requested = (size_t)(main_event.key_code - SAPP_KEYCODE_1);
      if (requested != selected_theme) {
        selected_theme = requested;
        theme_dirty = true;
      }
    }
    return EXIT_SUCCESS;
  }

  if (main_phase == PHASE_CLEANUP) {
    if (text_ready) snk_shutdown();
    if (font_atlas.temporary.alloc) nk_font_atlas_clear(&font_atlas);
    sg_shutdown();
    return EXIT_SUCCESS;
  }
  if (main_phase == PHASE_FRAME) {
    if (graphics_failed) {
      sapp_quit();
      return EXIT_FAILURE;
    }
    pace_frame();
    bool printed = false;
    for (size_t rule = 0; rule < rule_count; rule++) {
      if (!rules[rule].held) continue;
      for (size_t line = 0; line < rules[rule].info_count; line++) {
        (void)printf("%s\n", rules[rule].info[line]);
        printed = true;
      }
    }
    if (printed) (void)fflush(stdout);
    if (camera_left != camera_right) {
      float elapsed = (float)sapp_frame_duration();
      if (elapsed > 0.1f) elapsed = 0.1f;
      camera_x += (camera_right ? 960.0f : -960.0f) * elapsed;
      float const minimum_x = 0.0f;
      float const maximum_x =
          (float)(CAMERA_BATCH_COUNT - CAMERA_START_BATCH) * CAMERA_BATCH_WIDTH;
      if (camera_x < minimum_x) camera_x = minimum_x;
      if (camera_x > maximum_x) camera_x = maximum_x;
      camera_dirty = true;
    }
    bool const vertices_dirty = theme_dirty || camera_dirty;
    // Coalesce key events and upload each world's colors once per frame.
    if (theme_dirty) {
      Theme const* const theme = &themes[selected_theme];
      for (size_t channel = 0; channel < 3; channel++) {
        unsigned int const shift = 16U - 8U * (unsigned int)channel;
        color_background[channel] =
            (float)((theme->background >> shift) & 255U) / 255.0f;
      }
      pass_action.colors[0].clear_value = (sg_color){
          color_background[0], color_background[1], color_background[2], 1.0f};
      for (size_t world = 0; world < WORLD_COUNT; world++) {
        for (size_t corner = 0; corner < RECT_VERTEX_COUNT; corner++) {
          for (size_t channel = 0; channel < 3; channel++) {
            unsigned int const shift = 16U - 8U * (unsigned int)channel;
            worlds[world].vertices[corner].color[channel] =
                (float)((theme->rectangles[world % RECT_COLOR_COUNT] >> shift) &
                        255U) /
                255.0f;
          }
        }
      }
      char title[64];
      (void)snprintf(title, sizeof title, "di_do_bosonoga - %s", theme->name);
      sapp_set_window_title(title);
      theme_dirty = false;
    }
    if (vertices_dirty) {
      for (size_t world = 0; world < WORLD_COUNT; world++) {
        float const x = RECT_X + (float)world * RECT_SPACING - camera_x;
        for (size_t corner = 0; corner < RECT_VERTEX_COUNT; corner++) {
          bool const right = corner == 1 || corner == 2 || corner == 4;
          float const screen_x = x + (right ? RECT_WIDTH : 0.0f);
          worlds[world].vertices[corner].position[0] =
              2.0f * screen_x / (float)WINDOW_WIDTH - 1.0f;
        }
        sg_update_buffer(worlds[world].bindings.vertex_buffers[0],
                         &(sg_range){.ptr = worlds[world].vertices,
                                     .size = sizeof worlds[world].vertices});
      }
      camera_dirty = false;
    }
    sg_begin_pass(
        &(sg_pass){.action = pass_action, .swapchain = sglue_swapchain()});
    int current_batch =
        (int)((camera_x + (float)CAMERA_START_BATCH * CAMERA_BATCH_WIDTH) /
              CAMERA_BATCH_WIDTH);
    if (current_batch >= CAMERA_BATCH_COUNT)
      current_batch = CAMERA_BATCH_COUNT - 1;
    // Render the union of the current and valid neighboring batch ranges.
    // Membership changes only when CURRENT_BATCH changes; overlap draws once.
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
    // Submit associated rectangles regardless of visibility.
    for (size_t world = 0; world < WORLD_COUNT; world++) {
      float const world_x = RECT_X + (float)world * RECT_SPACING;
      if (world_x + RECT_WIDTH <= render_left || world_x >= render_right)
        continue;
      sg_apply_pipeline(worlds[world].pipeline);
      sg_apply_bindings(&worlds[world].bindings);
      sg_draw(0, RECT_VERTEX_COUNT, 1);
      triangle_batches++;
    }
    // Count scene batches, excluding the diagnostic overlay itself.
    char label[96];
    (void)snprintf(label, sizeof label, "CB: %d RB: %u CX: (%.1f)",
                   current_batch, triangle_batches, (double)camera_x);
    struct nk_context* ctx = snk_new_frame();
    if (nk_begin(ctx, "batch-count", nk_rect(8, 8, 640, 44),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
      nk_draw_text(nk_window_get_canvas(ctx), nk_rect(12, 10, 620, 32), label,
                   (int)strlen(label), &label_font->handle, nk_rgba(0, 0, 0, 0),
                   nk_rgb(255, 255, 255));
    }
    nk_end(ctx);
    snk_render(sapp_width(), sapp_height());
    sg_end_pass();
    sg_commit();
    return EXIT_SUCCESS;
  }
  if (main_phase == PHASE_INIT) {
    sg_setup(&(sg_desc){.environment = sglue_environment(),
                        .logger.func = slog_func});
    if (!sg_isvalid()) {
      graphics_failed = true;
      sapp_quit();
      return EXIT_FAILURE;
    }
    snk_setup(&(snk_desc_t){.no_default_font = true,
                            .max_vertices = 1024,
                            .dpi_scale = sapp_dpi_scale(),
                            .logger.func = slog_func});
    text_ready = true;
    nk_font_atlas_init_default(&font_atlas);
    nk_font_atlas_begin(&font_atlas);
    label_font = nk_font_atlas_add_from_file(
        &font_atlas, "asset/IndieFlower.ttf", 28.0f, NULL);
    if (!label_font) {
      fputs("Unable to load asset/IndieFlower.ttf\n", stderr);
      graphics_failed = true;
      sapp_quit();
      return EXIT_FAILURE;
    }
    int font_width, font_height;
    void const* pixels = nk_font_atlas_bake(&font_atlas, &font_width,
                                            &font_height, NK_FONT_ATLAS_RGBA32);
    if (!pixels) {
      graphics_failed = true;
      sapp_quit();
      return EXIT_FAILURE;
    }
    sg_image font_image = sg_make_image(&(sg_image_desc){
        .width = font_width,
        .height = font_height,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] = {.ptr = pixels,
                               .size = (size_t)font_width *
                                       (size_t)font_height * 4U},
        .label = "batch-label-font"});
    sg_view font_view =
        sg_make_view(&(sg_view_desc){.texture.image = font_image});
    snk_image_t font_handle =
        snk_make_image(&(snk_image_desc_t){.texture_view = font_view});
    nk_font_atlas_end(&font_atlas, snk_nkhandle(font_handle), NULL);
    nk_font_atlas_cleanup(&font_atlas);
    struct nk_context* ctx = snk_new_frame();
    nk_style_set_font(ctx, &label_font->handle);
    ctx->style.window.fixed_background =
        nk_style_item_color(nk_rgba(0, 0, 0, 0));
    if (sg_query_image_state(font_image) != SG_RESOURCESTATE_VALID ||
        sg_query_view_state(font_view) != SG_RESOURCESTATE_VALID) {
      graphics_failed = true;
    }
    char const* const buffer_labels[RECT_COLOR_COUNT] = {
        "red-world-vertices", "green-world-vertices", "blue-world-vertices"};
    char const* const pipeline_labels[RECT_COLOR_COUNT] = {
        "red-world-pipeline", "green-world-pipeline", "blue-world-pipeline"};
    for (size_t world = 0; world < WORLD_COUNT; world++) {
      World* const current = &worlds[world];
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
          sg_query_pipeline_state(current->pipeline) !=
              SG_RESOURCESTATE_VALID) {
        graphics_failed = true;
      }
    }
    pass_action = (sg_pass_action){
        .colors[0] = {.load_action = SG_LOADACTION_CLEAR,
                      .clear_value = {color_background[0], color_background[1],
                                      color_background[2], 1.0f}}};
    return graphics_failed ? EXIT_FAILURE : EXIT_SUCCESS;
  }

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
  char* program_content = malloc(BOSONO_LIMIT);
  if (!program_content) {
    fprintf(stderr, "Unable to allocate program buffer\n");
    (void)fclose(program_file);
    return EXIT_FAILURE;
  }
  size_t const program_length =
      fread(program_content, 1, PROGRAM_MAX_BYTES, program_file);
  int const extra_char = fgetc(program_file);
  if (ferror(program_file)) {
    fprintf(stderr, "Unable to read main.bosonoga\n");
    (void)fclose(program_file);
    free(program_content);
    return EXIT_FAILURE;
  }
  (void)fclose(program_file);
  if (extra_char != EOF) {
    fprintf(stderr, "main.bosonoga exceeds %d bytes\n", PROGRAM_MAX_BYTES);
    free(program_content);
    return EXIT_FAILURE;
  }
  program_content[program_length] = '\0';
  Tokens tokens = {0};
  int const tokenize_result = tokenize(&program_content, &tokens);
  if (tokenize_result < 0) {
    return EXIT_FAILURE;
  }
  printf("%d\n", parse(&tokens) + tokenize_result);
  rules[0] = (Rule){.key = SAPP_KEYCODE_W, .info = {"[W]"}, .info_count = 1};
  rules[1] = (Rule){.key = SAPP_KEYCODE_S, .info = {"[S]"}, .info_count = 1};
  rules[2] = (Rule){.key = SAPP_KEYCODE_Q, .info = {"[Q]"}, .info_count = 1};
  rules[3] = (Rule){.key = SAPP_KEYCODE_E, .info = {"[E]"}, .info_count = 1};
  rule_count = 4;

  uint32_t const* const colors = themes[selected_theme].rectangles;
  for (size_t channel = 0; channel < 3; channel++) {
    unsigned int const shift = 16U - 8U * (unsigned int)channel;
    color_background[channel] =
        (float)((themes[selected_theme].background >> shift) & 255U) / 255.0f;
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
      worlds[world].vertices[corner] = (Vertex){
          .position = {2.0f * corners[corner][0] / (float)WINDOW_WIDTH - 1.0f,
                       1.0f - 2.0f * corners[corner][1] / (float)WINDOW_HEIGHT,
                       0.0f},
          .color = {color_rect[0], color_rect[1], color_rect[2], 1.0f}};
    }
  }
  sapp_run(&(sapp_desc){.init_cb = main_init,
                        .frame_cb = main_frame,
                        .cleanup_cb = main_cleanup,
                        .event_cb = main_on_event,
                        .width = WINDOW_WIDTH,
                        .height = WINDOW_HEIGHT,
                        .swap_interval = 0,
                        .depth_format = SAPP_PIXELFORMAT_NONE,
                        .window_title = "di_do_bosonoga",
                        .fixed_size = true,
                        .icon.sokol_default = true,
                        .logger.func = slog_func});
  return graphics_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

// third-party
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
