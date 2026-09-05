// OpenAL's DSP component has no native log file, callback registration or
// device threads. Keep its formatted error reporting at the AudioWorklet's
// stderr boundary without importing a second formatter and C++ file streams.
#include "core/logging.h"

#include <cstdio>
#include <utility>

#ifndef __EMSCRIPTEN__
#error This logging adapter is only for the WebAssembly audio device.
#endif

LogLevel gLogLevel{LogLevel::Error};

void al_print_impl(LogLevel level, al::string_view format, al::format_args &&args)
{
    if (gLogLevel < level) return;
    const auto message = al::vformat(format, std::move(args));
    const char *prefix = level == LogLevel::Error ? "[ALSOFT] (EE) " :
        level == LogLevel::Warning ? "[ALSOFT] (WW) " :
        level == LogLevel::Trace ? "[ALSOFT] (II) " : "[ALSOFT] (--) ";
    std::fputs(prefix, stderr);
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}
