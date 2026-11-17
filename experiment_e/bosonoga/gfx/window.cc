// The window that runs a gfx program, as the C bosonoga's gfx.c: sokol_gl draws
// the scene that gfx_frame records, with fontstash for text and stb_image for
// icons, and after each frame gfx_main goes on until gfx_await_next_frame.
#include <fcntl.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

#define STBI_NO_STDIO
#include <stb_image.h>

#include "bosonoga_runtime.hh"

namespace bosonoga::runtime::gfx {
namespace {

constexpr std::int64_t frame_microseconds = 1000000 / 60;
constexpr unsigned hex_corners = 6;
// Enough that a circle of corela's size looks round.
constexpr unsigned circle_corners = 48;
constexpr int vertices_max = 1 << 18;
constexpr int icon_channels = 4;
// The smallest most that OpenGL 3.3 guarantees for a texture.
constexpr int icon_side_max = 1024;
// As much as a thread has.
constexpr std::size_t main_stack_size = std::size_t{8} << 20;

std::int64_t now_microseconds() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string file_error(const char *format, const char *path, int number) {
  return std::string(format) + "“" + path + "”: " + std::strerror(number);
}

// As g_file_get_contents, with its messages.
bool read_file(const char *path, std::string &contents, std::string &why) {
  const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    why = file_error("Failed to open file ", path, errno);
    return false;
  }
  struct stat status {};
  if (::fstat(fd, &status) < 0) {
    why = std::string("Failed to get attributes of file “") + path + "”: fstat() failed: " + std::strerror(errno);
    ::close(fd);
    return false;
  }
  const bool is_regular = status.st_size > 0 && S_ISREG(status.st_mode);
  std::string text;
  std::vector<char> buffer(65536);
  while (true) {
    const ssize_t count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      why = file_error(is_regular ? "Failed to read from file " : "Error reading file ", path, errno);
      ::close(fd);
      return false;
    }
    if (count == 0) {
      break;
    }
    text.append(buffer.data(), static_cast<std::size_t>(count));
  }
  ::close(fd);
  contents = std::move(text);
  return true;
}

std::uint32_t big_endian(const unsigned char *bytes, unsigned count) {
  std::uint32_t value = 0;
  for (unsigned i = 0; i < count; ++i) {
    value = value << 8 | bytes[i];
  }
  return value;
}

// stb_truetype crashes on a file that is not a font, so the tables that it
// reads first must be in the file: cmap, head, hhea and hmtx.
bool is_truetype(const unsigned char *data, std::size_t length) {
  static const struct {
    const char *tag;
    std::uint32_t size;
  } required[] = {{"cmap", 4}, {"head", 54}, {"hhea", 36}, {"hmtx", 4}};
  constexpr unsigned required_count = sizeof required / sizeof required[0];
  if (length < 12) {
    return false;
  }
  const std::uint32_t version = big_endian(data, 4);
  if (version != 0x00010000U && version != 0x74727565U /* true */ && version != 0x4F54544FU /* OTTO */) {
    return false;
  }
  const std::uint32_t table_count = big_endian(data + 4, 2);
  if (12 + 16 * static_cast<std::size_t>(table_count) > length) {
    return false;
  }
  unsigned found = 0;
  for (std::uint32_t i = 0; i < table_count; ++i) {
    const unsigned char *entry = data + 12 + 16 * i;
    const std::uint64_t offset = big_endian(entry + 8, 4);
    const std::uint64_t size = big_endian(entry + 12, 4);
    if (offset + size > length) {
      return false;
    }
    for (unsigned j = 0; j < required_count; ++j) {
      if (std::memcmp(entry, required[j].tag, 4) == 0) {
        if (size < required[j].size) {
          return false;
        }
        found |= 1U << j;
      }
    }
  }
  return found == (1U << required_count) - 1;
}

std::uint8_t channel(std::uint32_t color, unsigned shift) {
  return static_cast<std::uint8_t>((color >> shift) & 0xFFU);
}

void color(std::uint32_t rgb) {
  sgl_c3b(channel(rgb, 16), channel(rgb, 8), channel(rgb, 0));
}

void draw_rectangle(const Shape &rectangle) {
  const float left = rectangle.x;
  const float top = rectangle.y;
  const float right = rectangle.x + rectangle.width;
  const float bottom = rectangle.y + rectangle.height;
  sgl_begin_triangles();
  color(rectangle.color);
  sgl_v2f(left, top);
  sgl_v2f(right, top);
  sgl_v2f(right, bottom);
  sgl_v2f(left, top);
  sgl_v2f(right, bottom);
  sgl_v2f(left, bottom);
  sgl_end();
}

void draw_fan(const Shape &shape, unsigned corner_count) {
  const float step = static_cast<float>(2.0 * M_PI) / static_cast<float>(corner_count);
  sgl_begin_triangles();
  color(shape.color);
  for (unsigned i = 0; i < corner_count; ++i) {
    const float start = static_cast<float>(i) * step;
    const float end = static_cast<float>(i + 1) * step;
    sgl_v2f(shape.x, shape.y);
    sgl_v2f(shape.x + shape.radius * std::cos(start), shape.y + shape.radius * std::sin(start));
    sgl_v2f(shape.x + shape.radius * std::cos(end), shape.y + shape.radius * std::sin(end));
  }
  sgl_end();
}

// The flags of gfx_draw_text, as GfxStatements writes them.
int fons_align(std::uint32_t align) {
  static const struct {
    std::uint32_t align;
    int fons;
  } aligns[] = {
      {1U, FONS_ALIGN_LEFT}, {2U, FONS_ALIGN_CENTER},  {4U, FONS_ALIGN_RIGHT},     {8U, FONS_ALIGN_TOP},
      {16U, FONS_ALIGN_MIDDLE}, {32U, FONS_ALIGN_BOTTOM}, {64U, FONS_ALIGN_BASELINE},
  };
  int fons = 0;
  for (const auto &each : aligns) {
    if ((align & each.align) != 0) {
      fons |= each.fons;
    }
  }
  return fons;
}

// What sokol_app's callbacks share while the window runs, as the C bosonoga's
// BosonogaGfxRun.
struct Run {
  explicit Run(Window &running) : window(running) {}

  Window &window;
  FONScontext *fons = nullptr;
  std::vector<int> font_ids;
  std::vector<sg_image> icon_images;
  std::vector<sg_view> icon_views;
  sg_sampler icon_sampler{};
  sgl_pipeline icon_pipeline{};
  std::int64_t next_frame_time = 0;
  std::int64_t last_frame_start = 0;
  // gfx_main runs on a stack of its own, so that it can stop at
  // gfx_await_next_frame and go on from there after the next frame, as the C
  // bosonoga advances its execution of gfx_main. Its calls are counted apart.
  std::unique_ptr<char[]> main_stack;
  ucontext_t window_context{};
  ucontext_t main_context{};
  int main_call_depth = 0;
  bool is_main_ended = false;
  int exit_status = EXIT_FAILURE;
  std::string error;
  bool is_initialized = false;
  bool is_ended = false;
};

// sokol_app runs one window, and makecontext passes gfx_main no pointer.
Run *current = nullptr;

std::string prefix(const char *kind, const Asset &asset) {
  return std::string(current->window.setup.program) + ": " + kind + asset.key + ": ";
}

void fail(std::string message) {
  if (current->error.empty()) {
    current->error = std::move(message);
  }
  current->is_ended = true;
  sapp_quit();
}

bool call(const std::function<void()> &section) {
  if (!section) {
    return true;
  }
  try {
    section();
    return true;
  } catch (const std::exception &error) {
    fail(error.what());
    return false;
  }
}

void start_main() {
  try {
    current->exit_status = current->window.main();
  } catch (const std::exception &error) {
    if (current->error.empty()) {
      current->error = error.what();
    }
  }
  current->is_main_ended = true;
}

void sleep_in_main(std::uint32_t milliseconds) {
  const auto wake = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  while (std::chrono::steady_clock::now() < wake) {
    next_frame();
  }
}

void advance_main() {
  std::swap(call_depth(), current->main_call_depth);
  sleeping = sleep_in_main;
  ::swapcontext(&current->window_context, &current->main_context);
  sleeping = nullptr;
  std::swap(call_depth(), current->main_call_depth);
}

void on_init() {
  const Setup &setup = current->window.setup;
  sg_desc gfx{};
  gfx.environment = sglue_environment();
  gfx.logger.func = slog_func;
  sg_setup(&gfx);
  // Its pipelines must match the swapchain, which has no depth buffer.
  sgl_desc_t gl{};
  gl.max_vertices = vertices_max;
  gl.depth_format = SG_PIXELFORMAT_NONE;
  gl.logger.func = slog_func;
  sgl_setup(&gl);
  const sfons_desc_t fonts{};
  current->fons = sfons_create(&fonts);
  for (std::size_t i = 0; i < setup.fonts.size(); ++i) {
    std::string &data = current->window.font_data[i];
    // fontstash reads from the data, which the window keeps.
    const int id = fonsAddFontMem(current->fons, setup.fonts[i].key, reinterpret_cast<unsigned char *>(data.data()),
                                  static_cast<int>(data.size()), 0);
    if (id == FONS_INVALID) {
      fail(prefix("font_", setup.fonts[i]) + setup.fonts[i].path + " is not a TrueType font");
      return;
    }
    current->font_ids.push_back(id);
  }
  sg_sampler_desc sampler{};
  sampler.min_filter = SG_FILTER_LINEAR;
  sampler.mag_filter = SG_FILTER_LINEAR;
  sampler.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
  sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
  sampler.label = "bosonoga-icon-sampler";
  current->icon_sampler = sg_make_sampler(&sampler);
  sg_pipeline_desc pipeline{};
  pipeline.colors[0].blend.enabled = true;
  pipeline.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
  pipeline.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  pipeline.label = "bosonoga-icon-pipeline";
  current->icon_pipeline = sgl_make_pipeline(&pipeline);
  for (std::size_t i = 0; i < setup.icons.size(); ++i) {
    const IconPixels &pixels = current->window.icons[i];
    sg_image_desc image{};
    image.width = pixels.width;
    image.height = pixels.height;
    image.pixel_format = SG_PIXELFORMAT_RGBA8;
    image.data.ptr = pixels.rgba.data();
    image.data.size = pixels.rgba.size();
    image.label = setup.icons[i].key;
    current->icon_images.push_back(sg_make_image(&image));
    sg_view view_handle{};
    if (sg_query_image_state(current->icon_images.back()) == SG_RESOURCESTATE_VALID) {
      sg_view_desc view{};
      view.texture.image = current->icon_images.back();
      view.label = setup.icons[i].key;
      view_handle = sg_make_view(&view);
    }
    current->icon_views.push_back(view_handle);
    if (sg_query_view_state(view_handle) != SG_RESOURCESTATE_VALID) {
      fail(prefix("icon_", setup.icons[i]) + setup.icons[i].path + " cannot be made into a texture");
      return;
    }
  }
  current->is_initialized = call(current->window.init);
}

// Only holds back a display faster than 60 Hz. Each wait is measured from the
// previous frame time, so that errors do not add up.
void wait_for_frame_time() {
  const std::int64_t now = now_microseconds();
  if (now < current->next_frame_time) {
    std::this_thread::sleep_for(std::chrono::microseconds(current->next_frame_time - now));
    current->next_frame_time += frame_microseconds;
  } else {
    current->next_frame_time = now + frame_microseconds;
  }
}

void measure_delta_time() {
  const std::int64_t now = now_microseconds();
  const std::int64_t elapsed = current->last_frame_start == 0 ? frame_microseconds : now - current->last_frame_start;
  current->window.scene.delta_time = static_cast<float>(elapsed) / 1000000.0F;
  current->last_frame_start = now;
}

void draw_text(const Shape &text) {
  fonsSetFont(current->fons, current->font_ids[text.font]);
  fonsSetSize(current->fons, text.size);
  fonsSetColor(current->fons,
               sfons_rgba(channel(text.color, 16), channel(text.color, 8), channel(text.color, 0), 0xFF));
  fonsSetAlign(current->fons, fons_align(text.align));
  const char *start = current->window.scene.text.data() + text.text;
  fonsDrawText(current->fons, text.x, text.y, start, start + text.text_length);
}

// Turned about its center, which is moved so that, unturned, its corner is
// on a whole pixel.
void draw_icon(const Shape &shape) {
  const IconPixels &icon = current->window.icons[shape.icon];
  const float half_width = static_cast<float>(icon.width) / 2.0F;
  const float half_height = static_cast<float>(icon.height) / 2.0F;
  sgl_push_pipeline();
  sgl_load_pipeline(current->icon_pipeline);
  sgl_enable_texture();
  sgl_texture(current->icon_views[shape.icon], current->icon_sampler);
  sgl_matrix_mode_modelview();
  sgl_push_matrix();
  sgl_translate(std::round(shape.x - half_width) + half_width, std::round(shape.y - half_height) + half_height,
                0.0F);
  sgl_rotate(shape.angle, 0.0F, 0.0F, 1.0F);
  sgl_begin_quads();
  color(shape.color);
  sgl_v2f_t2f(-half_width, -half_height, 0.0F, 0.0F);
  sgl_v2f_t2f(half_width, -half_height, 1.0F, 0.0F);
  sgl_v2f_t2f(half_width, half_height, 1.0F, 1.0F);
  sgl_v2f_t2f(-half_width, half_height, 0.0F, 1.0F);
  sgl_end();
  sgl_pop_matrix();
  sgl_disable_texture();
  sgl_pop_pipeline();
}

// Shapes, texts and icons are one list of sokol_gl, so that each is drawn
// over the ones before it.
bool record_scene() {
  sgl_defaults();
  sgl_matrix_mode_projection();
  sgl_ortho(0.0F, sapp_widthf(), sapp_heightf(), 0.0F, -1.0F, 1.0F);
  for (const Shape &shape : current->window.scene.shapes) {
    switch (shape.kind) {
      case ShapeKind::rectangle:
        draw_rectangle(shape);
        break;
      case ShapeKind::hex:
        draw_fan(shape, hex_corners);
        break;
      case ShapeKind::circle:
        draw_fan(shape, circle_corners);
        break;
      case ShapeKind::text:
        draw_text(shape);
        break;
      case ShapeKind::icon:
        draw_icon(shape);
        break;
    }
  }
  sfons_flush(current->fons);
  // A full sokol_gl drops what does not fit, which would look like a frame
  // that works.
  if (sgl_error().any) {
    fail(std::string(current->window.setup.program) + ": gfx_frame draws more than " + std::to_string(vertices_max) +
         " vertices or commands");
    return false;
  }
  return true;
}

void render_scene() {
  const std::uint32_t clear = current->window.scene.clear_color.rgb;
  sg_pass pass{};
  pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
  pass.action.colors[0].clear_value = {static_cast<float>(channel(clear, 16)) / 255.0F,
                                       static_cast<float>(channel(clear, 8)) / 255.0F,
                                       static_cast<float>(channel(clear, 0)) / 255.0F, 1.0F};
  pass.swapchain = sglue_swapchain();
  sg_begin_pass(&pass);
  sgl_draw();
  sg_end_pass();
  sg_commit();
}

void on_frame() {
  if (current->is_ended) {
    return;
  }
  wait_for_frame_time();
  measure_delta_time();
  current->window.scene.reset(current->window.setup.background);
  if (!call(current->window.frame) || !record_scene()) {
    return;
  }
  render_scene();
  advance_main();
  if (current->is_main_ended) {
    current->is_ended = true;
    sapp_quit();
  }
}

void on_event(const sapp_event *event) {
  if (event->type != SAPP_EVENTTYPE_KEY_DOWN || event->key_repeat || !current->is_initialized || current->is_ended) {
    return;
  }
  current->window.scene.key = InputKey{static_cast<std::uint32_t>(event->key_code)};
  call(current->window.key_down);
}

void on_cleanup() {
  if (!current->is_ended) {
    fail(std::string(current->window.setup.program) + ": the window was closed before the program returned");
  }
  if (current->is_initialized) {
    call(current->window.cleanup);
  }
  for (std::size_t i = 0; i < current->icon_images.size(); ++i) {
    sg_destroy_view(current->icon_views[i]);
    sg_destroy_image(current->icon_images[i]);
  }
  sgl_destroy_pipeline(current->icon_pipeline);
  sg_destroy_sampler(current->icon_sampler);
  sfons_destroy(current->fons);
  sgl_shutdown();
  sg_shutdown();
}

// Sokol stops the program on what it cannot make, so the size of the PNG is
// checked first, and also so that a large one is not decoded. Only its alpha
// is kept, over white, so that the color of gfx_draw_icon tints it without
// dark edges where it is filtered.
IconPixels read_icon(const char *program, const Asset &asset) {
  const std::string prefix = std::string(program) + ": icon_" + asset.key + ": ";
  std::string data;
  std::string why;
  if (!read_file(asset.path, data, why)) {
    throw Error(prefix + why);
  }
  const auto *bytes = reinterpret_cast<const stbi_uc *>(data.data());
  IconPixels icon;
  int channels = 0;
  if (data.size() > INT_MAX ||
      !stbi_info_from_memory(bytes, static_cast<int>(data.size()), &icon.width, &icon.height, &channels)) {
    throw Error(prefix + asset.path + " is not a PNG image");
  }
  if (icon.width > icon_side_max || icon.height > icon_side_max) {
    throw Error(prefix + asset.path + " is " + std::to_string(icon.width) + " by " + std::to_string(icon.height) +
                " pixels, more than " + std::to_string(icon_side_max) + " a side");
  }
  stbi_uc *pixels =
      stbi_load_from_memory(bytes, static_cast<int>(data.size()), &icon.width, &icon.height, &channels, icon_channels);
  if (pixels == nullptr) {
    throw Error(prefix + asset.path + " is not a PNG image: " + stbi_failure_reason());
  }
  const std::size_t size = static_cast<std::size_t>(icon.width) * static_cast<std::size_t>(icon.height) *
                           static_cast<std::size_t>(icon_channels);
  icon.rgba.assign(pixels, pixels + size);
  stbi_image_free(pixels);
  for (std::size_t i = 0; i < size; i += icon_channels) {
    icon.rgba[i] = 0xFF;
    icon.rgba[i + 1] = 0xFF;
    icon.rgba[i + 2] = 0xFF;
  }
  return icon;
}

}  // namespace

Window::Window(Setup setup_of) : setup(std::move(setup_of)) {
  for (const Asset &font : setup.fonts) {
    const std::string prefix = std::string(setup.program) + ": font_" + font.key + ": ";
    std::string data;
    std::string why;
    if (!read_file(font.path, data, why)) {
      throw Error(prefix + why);
    }
    if (data.size() > INT_MAX || !is_truetype(reinterpret_cast<const unsigned char *>(data.data()), data.size())) {
      throw Error(prefix + font.path + " is not a TrueType font");
    }
    font_data.push_back(std::move(data));
  }
  for (const Asset &icon : setup.icons) {
    icons.push_back(read_icon(setup.program, icon));
  }
}

int Window::run() {
  Run state(*this);
  current = &state;
  state.main_stack.reset(new char[main_stack_size]);
  ::getcontext(&state.main_context);
  state.main_context.uc_stack.ss_sp = state.main_stack.get();
  state.main_context.uc_stack.ss_size = main_stack_size;
  state.main_context.uc_link = &state.window_context;
  ::makecontext(&state.main_context, start_main, 0);
  sapp_desc desc{};
  desc.init_cb = on_init;
  desc.frame_cb = on_frame;
  desc.cleanup_cb = on_cleanup;
  desc.event_cb = on_event;
  desc.width = setup.width;
  desc.height = setup.height;
  desc.depth_format = SAPP_PIXELFORMAT_NONE;
  desc.gl.major_version = 3;
  desc.gl.minor_version = 3;
  desc.window_title = setup.program;
  desc.logger.func = slog_func;
  // A window closed first leaves gfx_main where it stopped, as the C bosonoga
  // frees its execution of gfx_main unfinished.
  sapp_run(&desc);
  current = nullptr;
  if (!state.error.empty()) {
    throw Error(state.error);
  }
  return state.exit_status;
}

void next_frame() {
  ::swapcontext(&current->main_context, &current->window_context);
}

}  // namespace bosonoga::runtime::gfx
