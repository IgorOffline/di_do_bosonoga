#include "daria.h"

G_DEFINE_QUARK(daria_error, daria_error)

const gchar *daria_color_word(DariaColorName color) {
  static const gchar *const words[DARIA_COLOR_COUNT] = {
      [DARIA_COLOR_BACKGROUND] = "color_background",
      [DARIA_COLOR_RECTANGLE] = "color_rectangle",
  };
  return words[color];
}
