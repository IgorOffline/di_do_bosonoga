// Compatibility translation unit: all application logic is in ../main.c.
// The build needs only main.c; this file can also be listed harmlessly.
#include "gfx.h"
_Static_assert(GFX_MAX_TRIANGLES * 3 <= 2147483647,
               "vertex count must fit the graphics draw argument");
