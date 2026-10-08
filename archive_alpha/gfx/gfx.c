#include "gfx.h"
_Static_assert(GFX_MAX_TRIANGLES * 3 <= 2147483647,
               "vertex count must fit the graphics draw argument");
