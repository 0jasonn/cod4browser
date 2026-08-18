#include <gfx_d3d/gfx_world_types.h>

// Canonical renderer-world singleton. The full native db_registry.cpp owns
// this symbol today; the lifecycle-slice build keeps the same identity here.
GfxWorld s_world{};
