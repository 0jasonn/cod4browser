#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>

#include <cstdint>
#include <limits>

StringTable *varStringTable = nullptr;
StringTable **varStringTablePtr = nullptr;

namespace
{
void LoadStringTable(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varStringTable),
        sizeof(StringTable));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &varStringTable->name;
    Load_XString(false);
    if (DB_RuntimeGeneratedLoadFailed() || !varStringTable->values) return;
    const std::int64_t count = static_cast<std::int64_t>(
        varStringTable->rowCount) * varStringTable->columnCount;
    if (varStringTable->rowCount < 0 || varStringTable->columnCount < 0 ||
        count > (std::numeric_limits<std::int32_t>::max)() ||
        static_cast<std::uint64_t>(count) * sizeof(const char *) >
            (std::numeric_limits<std::uint32_t>::max)() ||
        !DB_RuntimeStreamCanRead(static_cast<std::size_t>(count) *
            sizeof(const char *)))
    {
        DB_RuntimeGeneratedFailure("StringTable/excessive values");
        return;
    }
    varStringTable->values = reinterpret_cast<const char **>(
        DB_AllocStreamPos(3));
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(
        const_cast<char **>(varStringTable->values)),
        static_cast<std::int32_t>(count * sizeof(const char *)));
    const char **entry = varStringTable->values;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varXString = entry;
        Load_XString(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}
} // namespace

void __cdecl Load_StringTablePtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varStringTablePtr), 4);
    if (DB_RuntimeGeneratedLoadFailed() || !*varStringTablePtr) return;
    const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(
        *varStringTablePtr);
    if (token == UINT32_MAX)
    {
        *varStringTablePtr = reinterpret_cast<StringTable *>(
            DB_AllocStreamPos(3));
        varStringTable = *varStringTablePtr;
        LoadStringTable(true);
        if (!DB_RuntimeGeneratedLoadFailed())
            Load_StringTableAsset(reinterpret_cast<XAssetHeader *>(
                varStringTablePtr));
        if (!DB_RuntimeGeneratedLoadFailed())
            DB_RuntimeTraceAssetLoaded((*varStringTablePtr)->name);
    }
    else DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
        varStringTablePtr));
}
