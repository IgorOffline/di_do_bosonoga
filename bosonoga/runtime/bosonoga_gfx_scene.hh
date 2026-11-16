#ifndef BOSONOGA_GFX_SCENE_HH
#define BOSONOGA_GFX_SCENE_HH

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "bosonoga_core.hh"
#include "bosonoga_value.hh"

namespace bosonoga::runtime::gfx {
enum class ShapeKind { rectangle, hex, circle, text, icon };
struct Shape {
  ShapeKind kind;
  float x = 0, y = 0, width = 0, height = 0, radius = 0, size = 0, angle = 0;
  std::uint32_t color = 0, font = 0, align = 0, icon = 0;
  std::size_t text = 0, text_length = 0;
};

class Text {
 public:
  template <typename T>
  Text &part(const Where &where, const T &value) {
    text_ += printed(value);
    if (text_.size() > 1024) fail(where, "gfx_draw_text is longer than 1024 bytes");
    return *this;
  }
  std::string take() { return std::move(text_); }
 private:
  std::string text_;
};

struct Scene {
  std::vector<Shape> shapes;
  std::string text;
  Color clear_color{0};
  float delta_time = 0;
  InputKey key{std::uint32_t{0}};

  void reset(Color background) {
    shapes.clear();
    text.clear();
    clear_color = background;
  }
  void clear(Color color) { clear_color = color; }
  void rectangle(float x, float y, float width, float height, Color color) {
    Shape shape{ShapeKind::rectangle};
    shape.x = x; shape.y = y; shape.width = width; shape.height = height; shape.color = color.rgb;
    shapes.push_back(shape);
  }
  void radial(ShapeKind kind, float x, float y, float radius, Color color) {
    if (radius > 0.0F) {
      Shape shape{kind};
      shape.x = x; shape.y = y; shape.radius = radius; shape.color = color.rgb;
      shapes.push_back(shape);
    }
  }
  void hex(float x, float y, float radius, Color color) { radial(ShapeKind::hex, x, y, radius, color); }
  void circle(float x, float y, float radius, Color color) { radial(ShapeKind::circle, x, y, radius, color); }
  void draw_text(float x, float y, Font font, float size, Color color, std::uint32_t align,
                 const std::string &value) {
    if (size > 0.0F) {
      Shape shape{ShapeKind::text};
      shape.x = x; shape.y = y; shape.font = font.index; shape.size = size;
      shape.color = color.rgb; shape.align = align; shape.text = text.size(); shape.text_length = value.size();
      text += value;
      shapes.push_back(shape);
    }
  }
  void draw_icon(float x, float y, Icon icon, float angle, Color color) {
    Shape shape{ShapeKind::icon};
    shape.x = x; shape.y = y; shape.icon = icon.index; shape.angle = angle; shape.color = color.rgb;
    shapes.push_back(shape);
  }
};

struct Context {
  Scene scene;
  std::function<void()> next_frame;
};
}  // namespace bosonoga::runtime::gfx
#endif
