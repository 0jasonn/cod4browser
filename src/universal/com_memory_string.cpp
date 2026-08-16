#include <script/scr_stringlist.h>
#include <universal/assertive.h>
#include <universal/com_memory.h>

const char *CopyString(const char *input)
{
    iassert(input);
    const std::uint32_t stringValue = SL_GetString_(input, 0, MT_TYPE_GENERIC);
    return SL_ConvertToString(stringValue);
}

void FreeString(const char *string)
{
    iassert(string);
    const std::uint32_t stringValue = SL_FindString(string);
    iassert(stringValue);
    SL_RemoveRefToString(stringValue);
}
