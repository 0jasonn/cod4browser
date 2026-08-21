#include <universal/q_shared.h>
#include "fx_system.h"

#include <database/database.h>
#include <database/db_registry_pools.h>
#include <database/db_registry_publication.h>

#include <physics/phys_local.h>

#include <cctype>
#include <cstdint>
#include <cstring>

namespace
{
bool FX_IsDiagnosticNameSane(const char *name)
{
    if (!name || !*name)
        return false;
    for (std::size_t index = 0; index < 128u; ++index)
    {
        const unsigned char value = static_cast<unsigned char>(name[index]);
        if (!value)
            return true;
        if (!std::isprint(value))
            return false;
    }
    return false;
}

std::uint32_t g_fxSaveNameDiagnostics = 0u;

struct FxRegistryDiagnostic
{
    std::uint32_t entryIndex = 0;
    std::uint32_t hashBucket = 0;
    std::uint32_t zoneIndex = 0;
    bool inuse = false;
    std::uint32_t poolIndex = UINT32_MAX;
    std::uint32_t nameZone = UINT32_MAX;
    std::uint32_t nameBlock = UINT32_MAX;
    std::uint32_t nameOffset = 0;
};

FxRegistryDiagnostic FX_FindRegistryDiagnostic(const FxEffectDef *effectDef)
{
    if (!effectDef) return {};
    for (std::uint32_t bucket = 0; bucket < 0x8000u; ++bucket)
    {
        for (std::uint32_t index = db_hashTable[bucket]; index;
             index = g_assetEntryPool[index].entry.nextHash)
        {
            const XAssetEntry &entry = g_assetEntryPool[index].entry;
            if (entry.asset.type == ASSET_TYPE_FX &&
                entry.asset.header.fx == effectDef)
            {
                FxRegistryDiagnostic result{index, bucket, entry.zoneIndex,
                    entry.inuse, DB_GetAssetPoolIndex(ASSET_TYPE_FX,
                        entry.asset.header), UINT32_MAX, UINT32_MAX, 0};
                const std::uintptr_t nameAddress =
                    reinterpret_cast<std::uintptr_t>(effectDef->name);
                for (std::uint32_t zone = 0; zone < ASSET_TYPE_COUNT; ++zone)
                {
                    for (std::uint32_t block = 0; block < 9; ++block)
                    {
                        const XBlock &candidate = g_zones[zone].mem.blocks[block];
                        if (!candidate.data || !candidate.size) continue;
                        const std::uintptr_t begin =
                            reinterpret_cast<std::uintptr_t>(candidate.data);
                        const std::uintptr_t end = begin + candidate.size;
                        if (nameAddress >= begin && nameAddress < end)
                        {
                            result.nameZone = zone;
                            result.nameBlock = block;
                            result.nameOffset = static_cast<std::uint32_t>(
                                nameAddress - begin);
                            return result;
                        }
                    }
                }
                return result;
            }
        }
    }
    return {};
}
}

void __cdecl FX_Restore(int32_t clientIndex, MemoryFile *memFile)
{
    int32_t v2; // [esp+0h] [ebp-201Ch] BYREF
    FxEffectDefTable table; // [esp+4h] [ebp-2018h] BYREF
    int32_t v4; // [esp+200Ch] [ebp-10h]
    int32_t relocationDistance; // [esp+2010h] [ebp-Ch]
    void *p; // [esp+2014h] [ebp-8h]
    FxSystemBuffers *systemBuffers; // [esp+2018h] [ebp-4h]

    p = FX_GetSystem(clientIndex);
    if (!p)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 220, 0, "%s", "system");
    systemBuffers = FX_GetSystemBuffers(clientIndex);
    if (!systemBuffers)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 223, 0, "%s", "systemBuffers");
    FX_RestoreEffectDefTable(memFile, &table);
    MemFile_ReadData(memFile, 2656, (uint8_t *)p);
    if (!*((_BYTE *)p + 2526) || *((uint32_t *)p + 627))
        Com_Error(ERR_DROP, "Invalid save file");
    FX_LinkSystemBuffers((FxSystem *)p, systemBuffers);
    MemFile_ReadData(memFile, 291968, (uint8_t *)systemBuffers);
    FX_FixupEffectDefHandles((FxSystem *)p, &table);
    MemFile_ReadData(memFile, 4, (uint8_t *)&v2);
    v4 = v2;
    relocationDistance = (int)p - v2;
    FX_RelocateSystem((FxSystem *)p, (int)p - v2);
    FX_RestorePhysicsData((FxSystem *)p, memFile);
    *((_BYTE *)p + 2526) = 0;
}

void __cdecl FX_RestoreEffectDefTable(MemoryFile *memFile, FxEffectDefTable *table)
{
    uint32_t p; // [esp+0h] [ebp-10h] BYREF
    const FxEffectDef *effectDef; // [esp+4h] [ebp-Ch]
    uint32_t key; // [esp+8h] [ebp-8h]
    const char *effectDefName; // [esp+Ch] [ebp-4h]
    uint32_t missingCount = 0;

    table->count = 0;
    while (1)
    {
        effectDefName = MemFile_ReadCString(memFile);
        if (!*effectDefName)
            break;
        MemFile_ReadData(memFile, 4, (uint8_t *)&p);
        key = p;
        effectDef = FX_Register((char *)effectDefName);
        if (!effectDef && missingCount++ < 4u)
            Com_PrintWarning(21,
                "FX_RestoreEffectDefTable: missing published effect '%s' key 0x%08x\n",
                effectDefName, key);
        FX_AddEffectDefTableEntry(table, key, effectDef);
    }
    if (missingCount > 4u)
        Com_PrintWarning(21,
            "FX_RestoreEffectDefTable: %u additional published effects missing\n",
            missingCount - 4u);
}

void __cdecl FX_AddEffectDefTableEntry(FxEffectDefTable *table, uint32_t key, const FxEffectDef *effectDef)
{
    if (!table)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 47, 0, "%s", "table");
    if (table->count >= 0x400u)
        MyAssertHandler(
            ".\\EffectsCore\\fx_archive.cpp",
            48,
            0,
            "table->count doesn't index ARRAY_COUNT( table->entries )\n\t%i not in [0, %i)",
            table->count,
            1024);
    if (!effectDef)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 49, 0, "%s", "effectDef");
    table->entries[table->count].key = key;
    table->entries[table->count++].effectDef = effectDef;
}

void __cdecl FX_FixupEffectDefHandles(FxSystem *system, FxEffectDefTable *table)
{
    const FxEffectDef *effectDef; // [esp+Ch] [ebp-10h]
    FxEffect *effect; // [esp+10h] [ebp-Ch]
    volatile int32_t activeIndex; // [esp+18h] [ebp-4h]

    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 131, 0, "%s", "system");
    if (!system->isArchiving)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 132, 0, "%s", "system->isArchiving");
    for (activeIndex = system->firstActiveEffect; activeIndex != system->firstNewEffect; ++activeIndex)
    {
        effect = FX_EffectFromHandle(system, system->allEffectHandles[activeIndex & 0x3FF]);
        effectDef = FX_FindEffectDefInTable(table, (uint32_t)effect->def);
        if (!effectDef)
            MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 139, 0, "%s", "effectDef");
        effect->def = effectDef;
    }
}

FxEffect *__cdecl FX_EffectFromHandle(FxSystem *system, uint16_t handle)
{
    const char *v2; // eax

    if (!system)
        MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 256, 0, "%s", "system");
    if (handle >= 0x8000u || handle % 0x20u)
    {
        v2 = va("%p %i", system->effects, handle);
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_system.h",
            257,
            0,
            "%s\n\t%s",
            "handle < FX_EFFECT_LIMIT * sizeof( FxEffect ) / FxEffect::HANDLE_SCALE && handle % (sizeof( FxEffect ) / FxEffect:"
            ":HANDLE_SCALE) == 0",
            v2);
    }
    return (FxEffect *)((char *)system->effects + 4 * handle);
}

const FxEffectDef *__cdecl FX_FindEffectDefInTable(const FxEffectDefTable *table, uint32_t key)
{
    int32_t index; // [esp+0h] [ebp-4h]

    for (index = 0; index < table->count; ++index)
    {
        if (table->entries[index].key == key)
            return table->entries[index].effectDef;
    }
    return 0;
}

void __cdecl FX_RestorePhysicsData(FxSystem *system, MemoryFile *memFile)
{
    const XModel *visuals; // [esp+18h] [ebp-20h]
    uint16_t elemHandle; // [esp+1Ch] [ebp-1Ch]
    const FxElemDef *elemDef; // [esp+20h] [ebp-18h]
    const FxEffect *effect; // [esp+24h] [ebp-14h]
    uint16_t elemHandleNext; // [esp+2Ch] [ebp-Ch]
    FxPool<FxElem> *elem; // [esp+30h] [ebp-8h]
    volatile int32_t activeIndex; // [esp+34h] [ebp-4h]

    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 185, 0, "%s", "system");
    if (!system->isArchiving)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 186, 0, "%s", "system->isArchiving");
    for (activeIndex = system->firstActiveEffect; activeIndex != system->firstNewEffect; ++activeIndex)
    {
        effect = FX_EffectFromHandle(system, system->allEffectHandles[activeIndex & 0x3FF]);
        for (elemHandle = effect->firstElemHandle[1]; elemHandle != 0xFFFF; elemHandle = elemHandleNext)
        {
            if (!system)
                MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 334, 0, "%s", "system");
            elem = FX_PoolFromHandle_Generic<FxElem, 2048>(system->elems, elemHandle);
            elemDef = &effect->def->elemDefs[elem->item.defIndex];
            elemHandleNext = elem->item.nextElemHandleInEffect;
            if (elemDef->elemType == 5 && (elemDef->flags & 0x8000000) != 0)
            {
                elem->item.physObjId = (int)Phys_ObjLoad(PHYS_WORLD_FX, memFile);
                visuals = FX_GetElemVisuals(
                    elemDef,
                    (296 * elem->item.sequence + elem->item.msecBegin + (uint32_t)effect->randomSeed) % 0x1DF).model;
                Phys_ObjSetCollisionFromXModel(visuals, PHYS_WORLD_FX, (dxBody *)elem->item.physObjId);
            }
        }
    }
}

FxElemVisuals __cdecl FX_GetElemVisuals(const FxElemDef *elemDef, int32_t randomSeed)
{
    if (!elemDef->visualCount)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_draw.h",
            79,
            0,
            "%s\n\t(elemDef->visualCount) = %i",
            "(elemDef->visualCount > 0)",
            elemDef->visualCount);
    if (elemDef->visualCount == 1)
        return elemDef->visuals.instance;
    else
        return (FxElemVisuals)elemDef->visuals.markArray->materials[(elemDef->visualCount
            * LOWORD(fx_randomTable[randomSeed + 21])) >> 16];
}

void __cdecl FX_Save(int32_t clientIndex, MemoryFile *memFile)
{
    uint32_t UsedSize; // eax
    uint32_t v3; // eax
    FxSystem *p; // [esp+0h] [ebp-Ch] BYREF
    FxSystem *system; // [esp+4h] [ebp-8h]
    FxSystemBuffers *systemBuffers; // [esp+8h] [ebp-4h]

    system = FX_GetSystem(clientIndex);
    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 265, 0, "%s", "system");
    systemBuffers = FX_GetSystemBuffers(clientIndex);
    if (!systemBuffers)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 267, 0, "%s", "systemBuffers");
    if (system->isArchiving)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 270, 0, "%s", "!system->isArchiving");
    system->isArchiving = 1;
    FX_SaveEffectDefTable(system, memFile);
    MemFile_WriteData(memFile, 2656, system);
    UsedSize = MemFile_GetUsedSize(memFile);
    // ProfMem_Begin("systemBuffers", UsedSize);
    MemFile_WriteData(memFile, 291968, systemBuffers);
    v3 = MemFile_GetUsedSize(memFile);
    // ProfMem_End(v3);
    p = system;
    MemFile_WriteData(memFile, 4, &p);
    FX_SavePhysicsData(system, memFile);
    system->isArchiving = 0;
}

void __cdecl FX_SaveEffectDefTable(FxSystem *system, MemoryFile *memFile)
{
    if (IsFastFileLoad())
        FX_SaveEffectDefTable_FastFile(memFile);
    else
        FX_SaveEffectDefTable_LoadObj(memFile);
    MemFile_WriteCString(memFile, "");
}

void __cdecl FX_SaveEffectDefTableEntry_FileLoadObj(const FxEffectDef* effectDef, MemoryFile* data)
{
    const FxEffectDef* p; // [esp+0h] [ebp-4h] BYREF

    if (!FX_IsDiagnosticNameSane(effectDef ? effectDef->name : nullptr) &&
        g_fxSaveNameDiagnostics++ < 4u)
    {
        std::uint32_t key = 0u;
        const FxRegistryDiagnostic registry =
            FX_FindRegistryDiagnostic(effectDef);
        std::memcpy(&key, &effectDef, sizeof(key));
        Com_PrintWarning(21,
            "FX_SaveEffectDefTable: malformed effect name at %p name %p key "
            "0x%08x pool %u entry %u bucket %u zone %u inuse %u nameZone "
            "%u block %u offset %u\n",
            effectDef, effectDef ? effectDef->name : nullptr, key,
            registry.poolIndex, registry.entryIndex, registry.hashBucket,
            registry.zoneIndex, registry.inuse ? 1u : 0u, registry.nameZone,
            registry.nameBlock, registry.nameOffset);
    }
    MemFile_WriteCString(data, (char*)effectDef->name);
    p = effectDef;
    MemFile_WriteData(data, 4, &p);
}

void __cdecl FX_SaveEffectDefTable_LoadObj(MemoryFile* memFile)
{
    FX_ForEachEffectDef((void(__cdecl*)(const FxEffectDef*, void*))FX_SaveEffectDefTableEntry_FileLoadObj, memFile);
}

void __cdecl FX_SaveEffectDefTable_FastFile(MemoryFile *memFile)
{
    DB_EnumXAssets(
        ASSET_TYPE_FX,
        (void(__cdecl *)(XAssetHeader, void *))FX_SaveEffectDefTableEntry_FileLoadObj,
        memFile,
        0);
}

void __cdecl FX_SavePhysicsData(FxSystem *system, MemoryFile *memFile)
{
    uint16_t elemHandle; // [esp+Ch] [ebp-18h]
    const FxElemDef *elemDef; // [esp+10h] [ebp-14h]
    const FxEffect *effect; // [esp+14h] [ebp-10h]
    uint16_t elemHandleNext; // [esp+18h] [ebp-Ch]
    FxPool<FxElem> *elem; // [esp+1Ch] [ebp-8h]
    volatile int32_t activeIndex; // [esp+20h] [ebp-4h]

    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 155, 0, "%s", "system");
    if (!system->isArchiving)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 156, 0, "%s", "system->isArchiving");
    for (activeIndex = system->firstActiveEffect; activeIndex != system->firstNewEffect; ++activeIndex)
    {
        effect = FX_EffectFromHandle(system, system->allEffectHandles[activeIndex & 0x3FF]);
        for (elemHandle = effect->firstElemHandle[1]; elemHandle != 0xFFFF; elemHandle = elemHandleNext)
        {
            if (!system)
                MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 334, 0, "%s", "system");
            elem = FX_PoolFromHandle_Generic<FxElem, 2048>(system->elems, elemHandle);
            elemDef = &effect->def->elemDefs[elem->item.defIndex];
            elemHandleNext = elem->item.nextElemHandleInEffect;
            if (elemDef->elemType == 5 && (elemDef->flags & 0x8000000) != 0)
                Phys_ObjSave((dxBody *)elem->item.physObjId, memFile);
        }
    }
}

void __cdecl FX_Archive(int32_t clientIndex, MemoryFile *memFile)
{
    if (MemFile_IsWriting(memFile))
        FX_Save(clientIndex, memFile);
    else
        FX_Restore(clientIndex, memFile);
}

