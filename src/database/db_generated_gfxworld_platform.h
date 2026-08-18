#pragma once

struct GfxWorld;

// Final-publication notification at the renderer platform boundary. The
// database retains ownership of the canonical GfxWorld graph.
void DB_PlatformPublishGfxWorld(const GfxWorld *world);
