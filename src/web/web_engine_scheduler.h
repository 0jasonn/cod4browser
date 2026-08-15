#pragma once

#include <web/web_cooperative_scheduler.h>
#include <web/web_system.h>

bool WebEngineScheduler_Initialize();
bool WebEngineScheduler_Register(
    const kisak::web::CooperativeTaskSpec &spec,
    kisak::web::CooperativeTaskHandle &handle);
void WebEngineScheduler_RunFrame(const WebFrameInfo &frame);
void WebEngineScheduler_Shutdown();
