#pragma once

#include <web/web_system.h>

// Cooperative browser adapter for the portable pre-database qcommon shell.
// Start/cancel are called when the validated asset-store generation changes;
// Frame advances no more than one startup action per RAF tick.
void WebQcommonRuntime_Frame(const WebFrameInfo &frame);

extern "C" void KisakWeb_StartQcommonRuntime();
extern "C" void KisakWeb_CancelQcommonRuntime();
