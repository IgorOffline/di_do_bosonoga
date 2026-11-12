// So that gfx.c only includes the declarations of the vendored headers.
#define SOKOL_IMPL
#define SOKOL_GLCORE
#define SOKOL_NO_ENTRY
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_gl.h>
#include <sokol_glue.h>
#include <sokol_log.h>

// fontstash.h uses these without including them.
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FONTSTASH_IMPLEMENTATION
#include <fontstash.h>

#define SOKOL_FONTSTASH_IMPL
#include <sokol_fontstash.h>

// Icons are PNG files, which gfx.c reads into memory.
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
