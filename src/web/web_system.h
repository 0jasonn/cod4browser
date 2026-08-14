#pragma once

#include <cstdint>

enum class WebLogLevel
{
    Info,
    Error,
};

struct WebFrameInfo
{
    uint32_t pumpTick;
    uint32_t monotonicMilliseconds;
};

using WebFrameCallback = void (*)(const WebFrameInfo &frame, void *userData);

// Browser-owned system services.  The frame pump invokes one callback per
// requestAnimationFrame and never blocks or spins to enforce a native FPS cap.
bool Web_StartFramePump(WebFrameCallback callback, void *userData);
void Web_Log(WebLogLevel level, const char *format, ...);

void Web_EmitRuntimeState(const char *state, const char *message);
void Web_EmitFrameStats(int frame, int width, int height, double elapsedSeconds);
void Web_EmitEngineState(
    const char *state,
    const char *commandDvar,
    const char *frameCommandDvar,
    uint32_t framePumpTick);
