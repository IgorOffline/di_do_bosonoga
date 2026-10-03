#ifndef CORE_REGINA_H
#define CORE_REGINA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core.h"
#include "parser.h"
#include "tokenizer.h"

#if defined(__APPLE__)
#define SOKOL_METAL
#else
#define SOKOL_VULKAN
#endif
#define SOKOL_NO_ENTRY
// clang-format off
#include "sokol_app.h"
#include "sokol_gfx.h"
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#include "nuklear.h"
// clang-format on

#define WORLD_COUNT 30
#define RECT_VERTEX_COUNT 6

#define REGINA_STATE_BYTES 67108864

typedef struct Vertex {
  float position[3];
  float color[4];
} Vertex;

typedef struct World {
  Vertex vertices[RECT_VERTEX_COUNT];
  sg_shader shader;
  sg_pipeline pipeline;
  sg_bindings bindings;
} World;

struct Regina {
  union {
    struct {
      size_t malloc_bytes;

      bool graphics_failed;
      bool text_ready;
      bool started;
      bool theme_dirty;
      bool camera_dirty;
      bool camera_left;
      bool camera_right;
      size_t selected_theme;
      float camera_x;
      float color_background[3];

      double next_frame;
      double fps_started_at;
      unsigned int fps_frames;
      char fps_label[32];

      sg_pass_action pass_action;
      struct nk_font_atlas font_atlas;
      struct nk_font* label_font;
      World worlds[WORLD_COUNT];

      bool rule_held[BOSONO_RULE_LIMIT];
      BosonoProgram script;
    };
    unsigned char state_slab[REGINA_STATE_BYTES];
  };
  Token tokens[BOSONO_TOKEN_LIMIT];
  char source[BOSONO_LIMIT];
};

_Static_assert(offsetof(Regina, tokens) == REGINA_STATE_BYTES,
               "Regina's state must fit its 64 MiB slab");
_Static_assert(sizeof(Token[BOSONO_TOKEN_LIMIT]) == 134217728,
               "Regina's tokens must fill their 128 MiB slab");
_Static_assert(BOSONO_LIMIT == 67108864,
               "Regina's source must fill its 64 MiB slab");
_Static_assert(sizeof(Regina) == REGINA_BYTES,
               "Regina's byte budget must sum to exactly 256 MiB");

#endif
