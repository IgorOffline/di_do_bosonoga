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
#define REGINA_RUNTIME_BYTES (40U * 1024U * 1024U)
#define REGINA_ASSET_GPU_BYTES REGINA_RUNTIME_BYTES

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

typedef struct AssetImage {
  sg_image image;
  sg_view view;
  int width;
  int height;
  uint32_t handle;
} AssetImage;

struct Regina {
  union {
    struct {
      size_t malloc_bytes;
      size_t runtime_bytes;
      size_t runtime_peak_bytes;
      size_t asset_gpu_bytes;
      AssetImage assets[BOSONO_ASSET_LIMIT];

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
      BosonoAliases aliases;
      BosonoProgram script;
    };
    unsigned char state_slab[REGINA_STATE_BYTES];
  };
  union {
    BosonoVariables variables;
    unsigned char variable_slab[BOSONO_VARIABLE_BYTES];
  };
  union {
    Token tokens[BOSONO_TOKEN_LIMIT];
    _Alignas(max_align_t) unsigned char runtime_pool[REGINA_RUNTIME_BYTES];
  };
  char source[BOSONO_LIMIT];
};

_Static_assert(offsetof(Regina, variables) == REGINA_STATE_BYTES,
               "Regina's state must fit its 64 MiB slab");
_Static_assert(BOSONO_VARIABLE_BYTES == REGINA_BYTES / 2,
               "Variables must reserve half of Regina");
_Static_assert(sizeof(BosonoVariables) <= BOSONO_VARIABLE_BYTES,
               "Typed variable storage must fit its reserved slab");
_Static_assert(offsetof(Regina, tokens) ==
                   REGINA_STATE_BYTES + BOSONO_VARIABLE_BYTES,
               "Regina's variables must fill their 120 MiB slab");
_Static_assert(sizeof(Token[BOSONO_TOKEN_LIMIT]) == 40U * 1024U * 1024U,
               "Regina's tokens must fill their 40 MiB slab");
_Static_assert(BOSONO_LIMIT == 16U * 1024U * 1024U,
               "Regina's source must fill its 16 MiB slab");
_Static_assert(sizeof(Regina) == REGINA_BYTES,
               "Regina's byte budget must sum to exactly 240 MiB");

#endif
