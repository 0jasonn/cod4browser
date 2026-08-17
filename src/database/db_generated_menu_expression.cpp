#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_generated_menu_internal.h>
#include <database/db_runtime_prefix.h>
#include <ui/ui_asset_types.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical menu expression loader requires the IW3 32-bit ABI");
static_assert(sizeof(Operand) == 8u);
static_assert(sizeof(expressionEntry) == 12u);
static_assert(sizeof(statement_s) == 8u);

bool CheckedPointerTableBytes(std::int32_t count, const char *stage,
    std::size_t &bytes)
{
    if (count < 0 || static_cast<std::uint32_t>(count) >
        (std::numeric_limits<std::uint32_t>::max)() / sizeof(void *))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    bytes = static_cast<std::size_t>(count) * sizeof(void *);
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    return true;
}

void LoadOperand(Operand *operand, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(operand),
        sizeof(*operand));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (operand->dataType == VAL_STRING)
    {
        varXString = &operand->internals.string;
        Load_XString(false);
    }
}

void LoadExpressionEntry(expressionEntry *entry, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(entry),
        sizeof(*entry));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (entry->type)
        LoadOperand(&entry->data.operand, false);
}
} // namespace

void DB_LoadGeneratedStatement(statement_s *statement, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(statement),
        sizeof(*statement));
    if (DB_RuntimeGeneratedLoadFailed() || !statement->entries) return;

    std::size_t bytes = 0;
    if (!CheckedPointerTableBytes(statement->numEntries,
        "Menu/statement entry table", bytes)) return;
    statement->entries = reinterpret_cast<expressionEntry **>(
        AllocLoad_FxElemVisStateSample());
    expressionEntry **entries = statement->entries;
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(entries),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;

    for (std::int32_t index = 0; index < statement->numEntries; ++index)
    {
        if (!entries[index]) continue;
        entries[index] = reinterpret_cast<expressionEntry *>(
            AllocLoad_FxElemVisStateSample());
        LoadExpressionEntry(entries[index], true);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}
