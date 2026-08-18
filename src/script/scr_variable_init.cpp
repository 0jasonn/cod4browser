#include <script/scr_main.h>
#include <script/scr_variable.h>
#include <script/scr_vm_runtime.h>

#include <qcommon/engine_lifecycle_trace.h>
#include <universal/q_shared.h>

#include <cstring>

scrVarPub_t scrVarPub{};
scrVarDebugPub_t *scrVarDebugPub = nullptr;
scrVarDebugPub_t scrVarDebugPubBuf{};
scrVarGlob_t scrVarGlob{};

scr_classStruct_t g_classMap[CLASS_NUM_COUNT] =
{
    {0, 0, 0x65, "entity"},
    {0, 0, 0x68, "hudelem"},
    {0, 0, 0x70, "pathnode"},
    {0, 0, 0x76, "vehiclenode"},
};

void Scr_InitVariables()
{
    EmitEngineLifecycleTrace(EngineLifecycleStage::ScriptVariablesInitBegin);
    if (!scrVarDebugPub)
        scrVarDebugPub = &scrVarDebugPubBuf;

    std::memset(scrVarDebugPub->leakCount, 0, sizeof(scrVarDebugPub->leakCount));
    scrVarPub.totalObjectRefCount = 0;
    scrVarPub.totalVectorRefCount = 0;

    if (scrVarDebugPub)
        std::memset(scrVarDebugPub->extRefCount, 0,
            sizeof(scrVarDebugPub->extRefCount));

    scrVarPub.numScriptValues = 0;
    scrVarPub.numScriptObjects = 0;

    if (scrVarDebugPub)
        std::memset(scrVarDebugPub, 0, 0x60000u);

    Scr_InitVariableRange(
        VARIABLELIST_PARENT_BEGIN, VARIABLELIST_PARENT_SIZE + 1);
    Scr_InitVariableRange(VARIABLELIST_CHILD_BEGIN, 0x18000u);
    EmitEngineLifecycleTrace(EngineLifecycleStage::ScriptVariablesInitComplete);
}

void Scr_InitVariableRange(std::uint32_t begin, std::uint32_t end)
{
    for (std::uint32_t index = begin + 1; index < end; ++index)
    {
        VariableValueInternal *value = &scrVarGlob.variableList[index];
        value->w.status = 0;
        iassert(!(value->w.type & VAR_MASK));
        value->hash.id = index - begin;
        value->v.next = index - begin;
        value->u.next = index - begin + 1;
        value->hash.u.prev = index - begin - 1;
    }

    VariableValueInternal *value = &scrVarGlob.variableList[begin];
    value->w.status = 0;
    iassert(!(value->w.type & VAR_MASK));
    value->hash.id = 0;
    value->v.next = 0;
    value->u.next = 1;
    scrVarGlob.variableList[begin + VARIABLELIST_PARENT_BEGIN].hash.u.prev = 0;
    value->hash.u.prev = end - begin - 1;
    scrVarGlob.variableList[end - 1].u.next = 0;
}

void Scr_InitClassMap()
{
    for (int classnum = 0; classnum < CLASS_NUM_COUNT; ++classnum)
    {
        g_classMap[classnum].entArrayId = 0;
        g_classMap[classnum].id = 0;
    }
}

std::uint32_t AllocValue()
{
    std::uint16_t index =
        scrVarGlob.variableList[VARIABLELIST_CHILD_BEGIN].u.next;
    if (!index)
        Scr_TerminalError("exceeded maximum number of script variables");

    VariableValueInternal *entry =
        &scrVarGlob.variableList[index + VARIABLELIST_CHILD_BEGIN];
    VariableValueInternal *entryValue =
        &scrVarGlob.variableList[entry->hash.id + VARIABLELIST_CHILD_BEGIN];
    iassert((entryValue->w.status & VAR_STAT_MASK) == VAR_STAT_FREE);

    std::uint16_t next = entryValue->u.next;
    if (entry != entryValue && (entry->w.status & VAR_STAT_MASK) == 0)
    {
        std::uint16_t newIndex = entry->v.next;
        iassert(newIndex != index);
        scrVarGlob.variableList[
            newIndex + VARIABLELIST_CHILD_BEGIN].hash.id = entry->hash.id;
        entry->hash.id = index;
        entryValue->v.next = newIndex;
        entryValue->u.next = entry->u.next;
        entryValue = &scrVarGlob.variableList[
            index + VARIABLELIST_CHILD_BEGIN];
    }
    scrVarGlob.variableList[VARIABLELIST_CHILD_BEGIN].u.next = next;
    scrVarGlob.variableList[next + VARIABLELIST_CHILD_BEGIN].hash.u.prev = 0;
    entryValue->v.next = index;
    entryValue->nextSibling = 0;
    entry->hash.u.prev = 0;

    iassert(entry->hash.id > 0 && entry->hash.id < VARIABLELIST_CHILD_SIZE);
    ++scrVarPub.totalObjectRefCount;
    if (scrVarDebugPub)
    {
        iassert(!scrVarDebugPub->leakCount[
            VARIABLELIST_CHILD_BEGIN + entry->hash.id]);
        ++scrVarDebugPub->leakCount[
            entry->hash.id + VARIABLELIST_CHILD_BEGIN];
    }
    ++scrVarPub.numScriptValues;
    iassert(scrVarPub.varUsagePos);
    if (scrVarDebugPub)
    {
        iassert(!scrVarDebugPub->varUsage[
            VARIABLELIST_CHILD_BEGIN + entry->hash.id]);
        scrVarDebugPub->varUsage[
            entry->hash.id + VARIABLELIST_CHILD_BEGIN] =
            scrVarPub.varUsagePos;
    }
    entryValue->w.status = VAR_STAT_EXTERNAL;
    iassert(!(entryValue->w.type & VAR_MASK));
    entryValue->w.status = static_cast<unsigned char>(entryValue->w.status);
    return entry->hash.id;
}
