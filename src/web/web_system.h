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
// Optional DevTools-owned, bounded benchmark sink. No events or allocations
// are produced during ordinary gameplay. While installed, it also requests
// every canonical scene view so fixed-work checkpoints can follow game time.
double Web_FrameTimingNow(bool beginFrame = false);
bool Web_FrameTimingActive();
void Web_RecordFrameScene(const char *world, std::uint32_t generation,
    std::int32_t time, bool geometry, std::uint32_t contextGeneration,
    std::uint32_t worldGeneration);
void Web_RecordFrameTiming(const WebFrameInfo &frame, double started,
    double simulationStarted, double submissionStarted,
    int wallMilliseconds, int simulationMilliseconds);
void Web_RequestQuit();
void Web_Log(WebLogLevel level, const char *format, ...);

void Web_EmitRuntimeState(const char *state, const char *message);
void Web_EmitFrameStats(int frame, int width, int height, double elapsedSeconds);
void Web_EmitEngineState(
    const char *state,
    const char *commandDvar,
    const char *frameCommandDvar,
    uint32_t framePumpTick);
