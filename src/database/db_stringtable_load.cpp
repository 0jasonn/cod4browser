#include <universal/q_shared.h>
#include "database.h"
#include <script/scr_stringlist.h>
#include <cstdint>

void __cdecl Load_ScriptStringCustom(uint16_t *var)
{
    *var = static_cast<std::uint16_t>(reinterpret_cast<std::uintptr_t>(
        varXAssetList->stringList.strings[*var]));
}

void __cdecl Mark_ScriptStringCustom(uint16_t *var)
{
    if (*var)
        SL_AddUser(*var, 4u);
}

