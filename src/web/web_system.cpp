#include <web/web_system.h>

#include <client/cl_input.h>
#include <client/client.h>
#include <qcommon/common_api.h>
#include <universal/q_shared.h>
#include <universal/com_math.h>
#include <qcommon/system_info.h>
#include <qcommon/qcommon_math.h>
#include <qcommon/sys_event_types.h>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <array>

namespace
{
constexpr std::size_t LOG_BUFFER_SIZE = 4096;

double g_timeBase = 0.0;
bool g_timeBaseInitialized = false;
bool g_framePumpStarted = false;
uint32_t g_framePumpTicks = 0;
WebFrameCallback g_frameCallback = nullptr;
void *g_frameUserData = nullptr;

enum class WebInputEventType : std::uint8_t
{
    Key,
    MouseMove,
};

struct WebInputEvent
{
    WebInputEventType type = WebInputEventType::Key;
    int value = 0;
    int value2 = 0;
    int value3 = 0;
    int value4 = 0;
    std::uint32_t time = 0u;
};

constexpr std::size_t INPUT_EVENT_CAPACITY = 512u;
std::array<WebInputEvent, INPUT_EVENT_CAPACITY> g_inputEvents{};
std::size_t g_inputReadIndex = 0u;
std::size_t g_inputWriteIndex = 0u;
std::size_t g_inputEventCount = 0u;
bool g_inputOverflowReported = false;
bool g_keyboardReceiptReported = false;
bool g_mouseReceiptReported = false;
bool g_mouseButtonReceiptReported = false;
bool g_mouseModePublished = false;
bool g_absoluteMouseMode = false;

bool QueueInputEvent(const WebInputEvent &event)
{
    if (g_inputEventCount == INPUT_EVENT_CAPACITY)
    {
        if (!g_inputOverflowReported)
        {
            Web_Log(WebLogLevel::Error,
                "[kisakcod-web] Browser input queue overflowed; dropping events.\n");
            g_inputOverflowReported = true;
        }
        return false;
    }
    g_inputEvents[g_inputWriteIndex] = event;
    g_inputWriteIndex = (g_inputWriteIndex + 1u) % INPUT_EVENT_CAPACITY;
    ++g_inputEventCount;
    return true;
}

bool DequeueInputEvent(WebInputEvent &event)
{
    if (g_inputEventCount == 0u)
        return false;
    event = g_inputEvents[g_inputReadIndex];
    g_inputReadIndex = (g_inputReadIndex + 1u) % INPUT_EVENT_CAPACITY;
    --g_inputEventCount;
    if (g_inputEventCount == 0u)
        g_inputOverflowReported = false;
    return true;
}

EM_JS(void, Web_PublishMouseMode, (int absolute), {
    globalThis.postMessage({
        type: "mouse-mode",
        absolute: Boolean(absolute),
    });
});

void PublishMouseModeIfChanged()
{
    constexpr int ABSOLUTE_MOUSE_CATCHERS = 0x2 | 0x10;
    const bool absolute =
        (clientUIActives[0].keyCatchers & ABSOLUTE_MOUSE_CATCHERS) != 0;
    if (g_mouseModePublished && absolute == g_absoluteMouseMode)
        return;
    g_mouseModePublished = true;
    g_absoluteMouseMode = absolute;
    Web_PublishMouseMode(absolute ? 1 : 0);
}

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
    (const char *state, uint32_t monotonicMilliseconds, uint32_t framePumpTicks,
     uint32_t callbackMilliseconds),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:system", {
            detail: {
                state: UTF8ToString(state),
                monotonicMilliseconds,
                framePumpTicks,
                callbackMilliseconds
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
    const uint32_t callbackStart = Sys_Milliseconds();
    g_frameCallback(frame, g_frameUserData);
    const uint32_t callbackMilliseconds = Sys_Milliseconds() - callbackStart;
    if (frame.pumpTick <= 2 || frame.pumpTick % 30 == 0 ||
        callbackMilliseconds >= 16u)
    {
        DispatchSystemStatus("running", frame.monotonicMilliseconds,
            frame.pumpTick, callbackMilliseconds);
    }
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

sysEvent_t *__cdecl Sys_GetEvent(sysEvent_t *result)
{
    iassert(result);
    *result = {};
    result->evTime = static_cast<int>(Sys_Milliseconds());
    result->evType = SE_NONE;
    return result;
}

SysInfo sys_info{
    1.0L, 1.0L, 1, 1, 1024, "WebGL2", true,
    "WebAssembly", "Browser Worker"
};

uint32_t __cdecl Sys_MillisecondsRaw()
{
    const auto now = static_cast<uint64_t>(emscripten_get_now());
    return static_cast<uint32_t>(now);
}

uint32_t __cdecl Sys_RawTimerTicks()
{
    return static_cast<uint32_t>(emscripten_get_now() * 1000.0);
}

void __cdecl Sys_LoadingKeepAlive()
{
    // Loading runs synchronously in the engine Worker. The browser main
    // thread and frame pump stay responsive without a native message pump.
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

void *Sys_VirtualReserve(std::size_t size)
{
    constexpr std::size_t alignment = 4096;
    const std::size_t alignedSize = (size + alignment - 1) & ~(alignment - 1);
    void *memory = std::aligned_alloc(alignment, alignedSize);
    if (memory)
    {
        // Native VirtualAlloc commit semantics expose zero-filled pages.
        // Wasm cannot reserve address space without backing it, so establish
        // that canonical allocator invariant at reservation time.
        std::memset(memory, 0, alignedSize);
    }
    return memory;
}

bool Sys_VirtualCommit(void *memory, std::size_t size)
{
    return memory != nullptr || size == 0;
}

void Sys_VirtualDecommit(void *, std::size_t)
{
    // Wasm linear memory cannot decommit an address range. The reservation is
    // retained and reused by the canonical zone/hunk allocator.
}

void Sys_VirtualRelease(void *memory)
{
    std::free(memory);
}

char *Sys_GetClipboardData()
{
    // Clipboard reads require an asynchronous browser permission boundary.
    // The synchronous engine API therefore reports no clipboard contents.
    return nullptr;
}

EM_JS(int, Web_SetClipboardData, (const char *text), {
    if (!navigator.clipboard || !navigator.clipboard.writeText) {
        return 0;
    }
    navigator.clipboard.writeText(UTF8ToString(text)).catch(() => {});
    return 1;
});

int __cdecl Sys_SetClipboardData(const char *text)
{
    return text ? Web_SetClipboardData(text) : 0;
}

void IN_Frame()
{
    // The DOM lives on the main thread while the canonical engine owns an
    // OffscreenCanvas in a Worker. Drain its narrow event queue at the same
    // per-client input seam used by the native platform implementation.
    WebInputEvent event{};
    while (DequeueInputEvent(event))
    {
        if (event.type == WebInputEventType::Key)
        {
            // Pointer-lock activation arrives as K_MOUSE1 before the first
            // physical keyboard event. Keep the one-shot keyboard proof for
            // an actual keyboard key so gameplay tests identify its binding.
            if (!g_keyboardReceiptReported && event.value < K_MOUSE1)
            {
                const char *binding = playerKeys[0].keys[event.value].binding;
                Web_Log(WebLogLevel::Info,
                    "[kisakcod-web] Browser keyboard reached canonical input "
                    "(key=%d, binding=%s, catchers=0x%x).\n",
                    event.value, binding ? binding : "<unbound>",
                    clientUIActives[0].keyCatchers);
                g_keyboardReceiptReported = true;
            }
            if (!g_mouseButtonReceiptReported && event.value == K_MOUSE1 &&
                event.value2)
            {
                const char *binding = playerKeys[0].keys[event.value].binding;
                Web_Log(WebLogLevel::Info,
                    "[kisakcod-web] Browser mouse button reached canonical "
                    "input (key=%d, binding=%s, catchers=0x%x).\n",
                    event.value, binding ? binding : "<unbound>",
                    clientUIActives[0].keyCatchers);
                g_mouseButtonReceiptReported = true;
            }
            CL_KeyEvent(0, event.value, event.value2, event.time);
        }
        else
        {
            if (!g_mouseReceiptReported)
            {
                Web_Log(WebLogLevel::Info,
                    "[kisakcod-web] Pointer-lock motion reached canonical input.\n");
                g_mouseReceiptReported = true;
            }
            CL_MouseEvent(event.value, event.value2, event.value3, event.value4);
        }
    }
    // Cursor visibility is not an input-mode signal: the canonical UI hides
    // the host cursor while drawing its own cursor inside the game viewport.
    // Publish the key-catcher-owned mode separately so the DOM host can keep
    // absolute menu motion unlocked without recapturing on a menu click.
    PublishMouseModeIfChanged();
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_QueueKeyEvent(int key, int down)
{
    if (key <= 0 || key >= 0xDF)
        return 0;
    return QueueInputEvent({
        WebInputEventType::Key,
        key,
        down ? 1 : 0,
        0,
        0,
        Sys_Milliseconds(),
    }) ? 1 : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_QueueMouseMove(
    int x, int y, int dx, int dy)
{
    return QueueInputEvent({
        WebInputEventType::MouseMove,
        x,
        y,
        dx,
        dy,
        Sys_Milliseconds(),
    }) ? 1 : 0;
}

EM_JS(void, Web_SetSystemCursorVisible, (int show), {
    globalThis.postMessage({
        type: "cursor",
        visible: Boolean(show),
    });
});

void __cdecl IN_ShowSystemCursor(int show)
{
    Web_SetSystemCursorVisible(show);
}

void __cdecl IN_SetForegroundWindow()
{
    // The engine already runs in the focused browser Worker/canvas pair.
}

EM_JS(void, Web_ActivateMouse, (), {
    const canvas = Module.canvas;
    if (canvas && canvas.requestPointerLock &&
        (typeof document === "undefined" || document.pointerLockElement !== canvas)) {
        try { canvas.requestPointerLock(); } catch (_) {}
    }
});

void __cdecl IN_ActivateMouse(int force)
{
    (void)force;
    Web_ActivateMouse();
}

void __cdecl Sys_SnapVector(float *value)
{
    value[0] = SnapFloat(value[0]);
    value[1] = SnapFloat(value[1]);
    value[2] = SnapFloat(value[2]);
}

void Sys_SuspendOtherThreads()
{
    // The initial browser runtime has one synchronous engine Worker.
}

int __cdecl Sys_IsRemoteDebugClient() { return 0; }
int __cdecl Sys_ReadDebugSocketInt() { return 0; }
void __cdecl Sys_WriteDebugSocketInt(int) {}
void __cdecl Sys_WriteDebugSocketString(char *) {}
void __cdecl Sys_WriteDebugSocketMessageType(unsigned char) {}
void __cdecl Sys_EndWriteDebugSocket() {}

namespace
{
bool g_allowClientMessages = true;
bool g_serverWakePending = false;
int g_serverTimeout = 0;
}

void Sys_ClientMessageReceived() {}
void Sys_AllowSendClientMessages() { g_allowClientMessages = true; }
void Sys_DisallowSendClientMessages() { g_allowClientMessages = false; }
bool Sys_WaitServerSnapshot() { return true; }
void Sys_WakeServer() { g_serverWakePending = true; }
void Sys_SleepServer() { g_serverWakePending = false; }
bool Sys_WaitServer() { return true; }
void Sys_SetServerTimeout(int timeout) { g_serverTimeout = timeout; }
bool Sys_WaitForSaveHistoryDone() { return true; }
void Sys_SetSaveHistoryEvent() {}
void Sys_WaitForSaveHistory() {}
void Sys_SetSaveHistoryDoneEvent() {}
int Sys_SpawnServerDemoThread(void(*)(std::uint32_t)) { return 0; }
void Sys_InitServerEvents()
{
    g_allowClientMessages = true;
    g_serverWakePending = false;
    g_serverTimeout = 0;
}
void __cdecl Sys_EndLoadThreadPriorities() {}

char *__cdecl Sys_DefaultInstallPath()
{
    static char path[] = "/";
    return path;
}

void Sys_Sleep(std::uint32_t)
{
    // Blocking the engine Worker cannot advance another engine execution
    // context. All admitted browser runtime paths are cooperative instead.
}

int __cdecl LiveStorage_GetStat(int, int) { return 0; }
void __cdecl LiveStorage_NewUser() {}

EM_JS(void, Web_OpenURL, (const char *url), {
    const value = UTF8ToString(url);
    if (typeof globalThis.open === "function") {
        globalThis.open(value, "_blank", "noopener");
    } else if (typeof globalThis.postMessage === "function") {
        globalThis.postMessage({ type: "kisakcod:open-url", url: value });
    }
});

void __cdecl Sys_OpenURL(const char *url, int)
{
    if (url && *url)
    {
        Web_OpenURL(url);
    }
}

EM_JS(void, Web_GetHardwareDescription,
    (char *gpu, std::size_t gpuSize, char *vendor, std::size_t vendorSize,
     char *name, std::size_t nameSize), {
        let gpuName = "WebGL2";
        try {
            const canvas = Module.canvas;
            const gl = canvas && canvas.getContext && canvas.getContext("webgl2");
            const ext = gl && gl.getExtension("WEBGL_debug_renderer_info");
            if (gl && ext) gpuName = gl.getParameter(ext.UNMASKED_RENDERER_WEBGL);
        } catch (_) {}
        stringToUTF8(gpuName, gpu, gpuSize);
        stringToUTF8("WebAssembly", vendor, vendorSize);
        stringToUTF8((typeof navigator !== "undefined" && navigator.userAgent) || "Browser", name, nameSize);
    });

void Sys_GetHardwareDescription(char *gpu, std::size_t gpuSize,
    char *cpuVendor, std::size_t cpuVendorSize,
    char *cpuName, std::size_t cpuNameSize)
{
    Web_GetHardwareDescription(gpu, gpuSize, cpuVendor, cpuVendorSize,
        cpuName, cpuNameSize);
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
    DispatchSystemStatus("ready", Sys_Milliseconds(), 0, 0);
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
