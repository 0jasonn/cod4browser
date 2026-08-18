#include <xanim/dobj_runtime_init.h>

#include <qcommon/engine_lifecycle_trace.h>
#include <script/scr_stringlist.h>

#include <cstring>

std::uint32_t g_empty = 0;

void __cdecl DObjInit()
{
    EmitEngineLifecycleTrace(EngineLifecycleStage::DObjInitBegin);
    int duplicatePartBits[5];
    std::memset(duplicatePartBits, 0, sizeof(duplicatePartBits));
    g_empty = SL_GetStringOfSize(
        reinterpret_cast<char *>(duplicatePartBits),
        0,
        0x11u,
        MT_TYPE_DUPLICATE_PARTS);
    EmitEngineLifecycleTrace(EngineLifecycleStage::DObjInitComplete);
}
