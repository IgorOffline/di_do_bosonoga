#ifndef GFX_GLSL_H
#define GFX_GLSL_H

// clang-format off
#include "sokol_gfx.h"
#if defined(SOKOL_METAL)
#define GFX_ATTR_POSITION 0
#define GFX_ATTR_COLOR 1
#define GFX_METAL_SOURCE \
  "#include <metal_stdlib>\n" \
  "using namespace metal;\n" \
  "struct Input { float3 position [[attribute(0)]]; " \
  "float4 color [[attribute(1)]]; };\n" \
  "struct Output { float4 position [[position]]; float4 color; };\n" \
  "vertex Output vertex_main(Input input [[stage_in]]) { " \
  "Output output; output.position = float4(input.position, 1.0); " \
  "output.color = input.color; return output; }\n" \
  "fragment float4 fragment_main(Output input [[stage_in]]) { " \
  "return input.color; }\n"
#define GFX_MAKE_SHADER() sg_make_shader(&(sg_shader_desc){ \
  .vertex_func = {.source = GFX_METAL_SOURCE, .entry = "vertex_main"}, \
  .fragment_func = {.source = GFX_METAL_SOURCE, .entry = "fragment_main"}, \
  .attrs = {[0].base_type = SG_SHADERATTRBASETYPE_FLOAT, \
            [1].base_type = SG_SHADERATTRBASETYPE_FLOAT}, \
  .label = "triangle-metal"})
#elif defined(SOKOL_VULKAN)
#include "gfx.glsl.gen.h"
#define GFX_ATTR_POSITION ATTR_triangle_position
#define GFX_ATTR_COLOR ATTR_triangle_color0
#define GFX_MAKE_SHADER() \
  sg_make_shader(triangle_shader_desc(SG_BACKEND_VULKAN))
#else
#error "Supported: Metal on macOS and Vulkan on Windows/Linux."
#endif
// clang-format on

#endif
