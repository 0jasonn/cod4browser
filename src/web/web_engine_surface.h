#pragma once

#include <cstdint>

struct WebFrameInfo;

enum class WebEngineSurfaceFrameResult : std::uint8_t
{
    Pending = 0,
    Ready,
    Failed,
};

// Builds one deterministic synthetic fastfile and transfers its byte allocation
// into the incremental extractor. No zlib traversal or renderer work runs here.
bool WebEngineSurface_Start();

// Advances exactly one bounded extraction step for this browser frame. Once the
// job succeeds, conversion remains above the renderer boundary and submission is
// callback-scoped; the renderer owns the independent recovery description after
// WebRenderer_SetSurface returns.
WebEngineSurfaceFrameResult WebEngineSurface_Frame(const WebFrameInfo &frame);
