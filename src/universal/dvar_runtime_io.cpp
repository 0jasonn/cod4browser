// Canonical dvar.cpp persistence parser, split out while the Gate 3 prefix
// excludes this tail of the ordinary translation unit.

#include <qcommon/qcommon.h>
#include <universal/dvar.h>
#include <universal/q_parse.h>

#include <array>
#include <cstring>

int __cdecl Com_LoadDvarsFromBuffer(
    const char **dvarnames, std::uint32_t numDvars,
    char *buffer, char *filename)
{
    iassert(numDvars < 0x4000);
    std::array<std::uint8_t, 0x4004> wasRead{};
    std::uint32_t readCount = 0;

    for (std::uint32_t index = 0; index < numDvars; ++index)
    {
        auto *dvar = const_cast<dvar_s *>(Dvar_FindVar(dvarnames[index]));
        iassert(dvar);
        Dvar_Reset(dvar, DVAR_SOURCE_INTERNAL);
    }

    Com_BeginParseSession(filename);
    while (true)
    {
        char *name = reinterpret_cast<char *>(Com_Parse(&buffer));
        if (!*name) break;

        std::uint32_t index = 0;
        for (; index < numDvars; ++index)
            if (!I_stricmp(name, dvarnames[index])) break;
        if (index == numDvars)
        {
            Com_PrintWarning(16,
                "WARNING: unknown dvar '%s' in file '%s'\n", name, filename);
            Com_SkipRestOfLine(const_cast<const char **>(&buffer));
            continue;
        }

        auto *dvar = const_cast<dvar_s *>(Dvar_FindVar(dvarnames[index]));
        iassert(dvar);
        char *value = reinterpret_cast<char *>(Com_ParseOnLine(&buffer));
        Dvar_SetFromString(dvar, value);
        if (!wasRead[index])
        {
            wasRead[index] = 1;
            ++readCount;
        }
        Com_SkipRestOfLine(const_cast<const char **>(&buffer));
    }
    Com_EndParseSession();

    if (readCount == numDvars) return 1;
    Com_PrintError(16,
        "ERROR: the following dvars were not specified in file '%s'\n",
        filename);
    for (std::uint32_t index = 0; index < numDvars; ++index)
        if (!wasRead[index]) Com_PrintError(16, "  %s\n", dvarnames[index]);
    return 0;
}
