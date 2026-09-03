// WebAssembly has fixed IEEE-754 rounding and no writable SSE control
// register. The DSP still uses Emscripten's SSE numeric compatibility path;
// only OpenAL's host register save/restore is replaced at this platform seam.
#include "core/fpu_ctrl.h"

#ifndef __EMSCRIPTEN__
#error This FPU adapter is only for the WebAssembly audio device.
#endif

unsigned int FPUCtl::Set() noexcept { return 0; }
void FPUCtl::Reset(unsigned int) noexcept {}
