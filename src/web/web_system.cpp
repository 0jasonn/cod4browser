#include <web/web_system.h>

#include <qcommon/common_api.h>
#include <universal/q_shared.h>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <cstdarg>
#include <cstdlib>
#include <cstdio>

namespace
{
constexpr std::size_t LOG_BUFFER_SIZE = 4096;

double g_timeBase = 0.0;
bool g_timeBaseInitialized = false;
bool g_framePumpStarted = false;
uint32_t g_framePumpTicks = 0;
WebFrameCallback g_frameCallback = nullptr;
void *g_frameUserData = nullptr;

EM_JS(void, DispatchRuntimeState, (const char *state, const char *message), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:state", {
        detail: {
            state: UTF8ToString(state),
            message: UTF8ToString(message)
        }
    }));
});

EM_JS(void, DispatchFrameStats, (int frame, int width, int height, double elapsed), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:frame", {
        detail: { frame, width, height, elapsed }
    }));
});

EM_JS(
    void,
    DispatchSystemStatus,
    (const char *state, uint32_t monotonicMilliseconds, uint32_t framePumpTicks),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:system", {
            detail: {
                state: UTF8ToString(state),
                monotonicMilliseconds,
                framePumpTicks
            }
        }));
    });

EM_JS(
    void,
    DispatchEngineState,
    (const char *state,
     const char *commandDvar,
     const char *frameCommandDvar,
     uint32_t framePumpTick),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:engine", {
            detail: {
                state: UTF8ToString(state),
                commandDvar: UTF8ToString(commandDvar),
                frameCommandDvar: UTF8ToString(frameCommandDvar),
                framePumpTick
            }
        }));
    });

void FramePumpTrampoline(void *)
{
    ++g_framePumpTicks;
    const WebFrameInfo frame{g_framePumpTicks, Sys_Milliseconds()};
    if (frame.pumpTick <= 2 || frame.pumpTick % 30 == 0)
    {
        DispatchSystemStatus("running", frame.monotonicMilliseconds, frame.pumpTick);
    }
    g_frameCallback(frame, g_frameUserData);
}

void PrintFormatted(FILE *stream, const char *format, va_list arguments)
{
    char buffer[LOG_BUFFER_SIZE]{};
    std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    buffer[sizeof(buffer) - 1] = '\0';
    std::fputs(buffer, stream);
    std::fflush(stream);
}
} // namespace

uint32_t __cdecl Sys_MillisecondsRaw()
{
    const auto now = static_cast<uint64_t>(emscripten_get_now());
    return static_cast<uint32_t>(now);
}

uint32_t __cdecl Sys_Milliseconds()
{
    const double now = emscripten_get_now();
    if (!g_timeBaseInitialized)
    {
        g_timeBase = now;
        g_timeBaseInitialized = true;
    }
    return static_cast<uint32_t>(static_cast<uint64_t>(now - g_timeBase));
}

void __cdecl Sys_Print(const char *text)
{
    if (!text)
    {
        return;
    }
    std::fputs(text, stdout);
    std::fflush(stdout);
}

void Sys_Error(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    PrintFormatted(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
    std::abort();
}

void Sys_OutOfMemErrorInternal(const char *filename, int line)
{
    Sys_Error("Out of memory at %s:%d", filename ? filename : "?", line);
}

void *Sys_AllocatePhysicalMemory(std::size_t size, std::size_t alignment)
{
    if (!size || !alignment || (alignment & (alignment - 1)) != 0 || size % alignment != 0)
    {
        return nullptr;
    }
    return std::aligned_alloc(alignment, size);
}

void Sys_FreePhysicalMemory(void *memory)
{
    std::free(memory);
}

void Web_Log(WebLogLevel level, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    PrintFormatted(level == WebLogLevel::Error ? stderr : stdout, format, arguments);
    va_end(arguments);
}

bool Web_StartFramePump(WebFrameCallback callback, void *userData)
{
    if (!callback || g_framePumpStarted)
    {
        Web_Log(WebLogLevel::Error, "[kisakcod-web] Invalid or duplicate frame pump start.\n");
        return false;
    }

    g_frameCallback = callback;
    g_frameUserData = userData;
    g_framePumpStarted = true;
    g_framePumpTicks = 0;
    DispatchSystemStatus("ready", Sys_Milliseconds(), 0);
    emscripten_set_main_loop_arg(FramePumpTrampoline, nullptr, 0, EM_FALSE);
    return true;
}

void Web_EmitRuntimeState(const char *state, const char *message)
{
    DispatchRuntimeState(state, message);
}

void Web_EmitFrameStats(int frame, int width, int height, double elapsedSeconds)
{
    DispatchFrameStats(frame, width, height, elapsedSeconds);
}

void Web_EmitEngineState(
    const char *state,
    const char *commandDvar,
    const char *frameCommandDvar,
    uint32_t framePumpTick)
{
    DispatchEngineState(state, commandDvar, frameCommandDvar, framePumpTick);
}
