// The window that a generated gfx program runs in, as the C bosonoga's gfx.c
// runs one: sokol calls it back for each frame, which gfx_frame draws, and
// after which gfx_main goes on until gfx_await_next_frame. It is defined with
// sokol in gfx/window.cc, so that a program that is only compiled needs no
// window libraries.
#ifndef BOSONOGA_GFX_WINDOW_HH
#define BOSONOGA_GFX_WINDOW_HH

#include <functional>
#include <string>
#include <vector>

#include "bosonoga_gfx_scene.hh"
#include "bosonoga_value.hh"

namespace bosonoga::runtime::gfx {

// A font or an icon of regina.ini that the program names, in the order of its index.
struct Asset {
  const char *key;
  const char *path;
};

struct Setup {
  // As errors and the window title show the program.
  const char *program;
  int width;
  int height;
  Color background;
  std::vector<Asset> fonts;
  std::vector<Asset> icons;
};

struct IconPixels {
  std::vector<unsigned char> rgba;
  int width = 0;
  int height = 0;
};

class Window {
 public:
  // Reads and checks the fonts and icons, as the C bosonoga loads them before
  // bosonoga_global.
  explicit Window(Setup setup);
  // Runs until gfx_main returns, and gives its exit status. A failure is
  // thrown as an Error.
  int run();

  Setup setup;
  Scene scene;
  // An empty section is one that the program does not have.
  std::function<void()> init;
  std::function<void()> frame;
  std::function<void()> key_down;
  std::function<void()> cleanup;
  std::function<int()> main;
  std::vector<std::string> font_data;
  std::vector<IconPixels> icons;
};

// gfx_await_next_frame: gfx_main stops here until the window has drawn the
// next frame.
void next_frame();

}  // namespace bosonoga::runtime::gfx

#endif
