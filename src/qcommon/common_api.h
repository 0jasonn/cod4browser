#pragma once

#include <universal/q_shared.h>

// The small platform-neutral surface needed by the command and dvar cores.
// Full engine builds provide these through common.cpp and their platform
// system layer; the browser target provides them through web_system.cpp.
void QDECL Com_Printf(int channel, const char *format, ...);
void __cdecl Sys_Print(const char *text);
