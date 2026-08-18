#include <xanim/xanim_runtime_init.h>

#include <qcommon/engine_lifecycle_trace.h>
#include <script/scr_stringlist.h>

int g_info_usage = 0;
int g_info_high_usage = 0;
int g_notifyListSize = 0;
std::uint32_t g_endNotetrackName = 0;
bool g_anim_developer = false;
XAnimNotify_s g_notifyList[0x80]{};
XAnimInfo g_xAnimInfo[0x1000]{};

void __cdecl XAnimInit()
{
    EmitEngineLifecycleTrace(EngineLifecycleStage::XAnimInitBegin);
    for (int i = 0; i < 4096; ++i)
    {
        g_xAnimInfo[i].prev = (i + 4095) % 4096;
        g_xAnimInfo[i].next = (i + 1) % 4096;
    }
    g_xAnimInfo[0].state.currentAnimTime = 0.0f;
    g_xAnimInfo[0].state.oldTime = 0.0f;
    g_xAnimInfo[0].state.cycleCount = 0;
    g_xAnimInfo[0].state.oldCycleCount = 0;
    g_xAnimInfo[0].state.goalTime = 0.0f;
    g_xAnimInfo[0].state.goalWeight = 0.0f;
    g_xAnimInfo[0].state.weight = 0.0f;
    g_xAnimInfo[0].state.rate = 0.0f;
    g_xAnimInfo[0].state.instantWeightChange = false;
    g_endNotetrackName = SL_GetString_("end", 0, MT_TYPE_NOTETRACK);
    g_anim_developer = true;
    g_info_usage = 1;
    g_info_high_usage = 1;
    EmitEngineLifecycleTrace(EngineLifecycleStage::XAnimInitComplete);
}
