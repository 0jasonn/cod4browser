#include <universal/dvar.h>

#include <qcommon/cmd.h>
#include <qcommon/common_api.h>

#include <array>
#include <cctype>
#include <cstring>
#include <string>

namespace
{
constexpr std::size_t DVAR_HASH_COUNT = 256;
constexpr std::size_t DVAR_POOL_COUNT = 4096;
constexpr std::size_t DVAR_COMMAND_LENGTH = 4096;

struct DvarStorage
{
    dvar_s dvar{};
    std::string name;
    std::string description;
    std::string current;
    std::string latched;
    std::string reset;
};

std::array<DvarStorage, DVAR_POOL_COUNT> g_dvarPool;
std::array<dvar_s *, DVAR_HASH_COUNT> g_dvarHashTable{};
std::size_t g_dvarCount = 0;
bool g_dvarSystemActive = false;
cmd_function_s g_setCommand{};

unsigned char AsciiLower(unsigned char value)
{
    if (value >= 'A' && value <= 'Z')
    {
        return static_cast<unsigned char>(value + ('a' - 'A'));
    }
    return value;
}

bool NamesEqual(const char *lhs, const char *rhs)
{
    if (!lhs || !rhs)
    {
        return lhs == rhs;
    }

    while (*lhs && *rhs)
    {
        if (AsciiLower(static_cast<unsigned char>(*lhs)) !=
            AsciiLower(static_cast<unsigned char>(*rhs)))
        {
            return false;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

std::size_t HashName(const char *name)
{
    unsigned int hash = 0;
    for (std::size_t index = 0; name && name[index]; ++index)
    {
        hash += AsciiLower(static_cast<unsigned char>(name[index])) *
            static_cast<unsigned int>(index + 119);
    }
    return hash & (DVAR_HASH_COUNT - 1);
}

bool IsValidName(const char *name)
{
    if (!name || !*name)
    {
        return false;
    }

    for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(name);
         *cursor;
         ++cursor)
    {
        if (!std::isalnum(*cursor) && *cursor != '_')
        {
            return false;
        }
    }
    return true;
}

DvarStorage *StorageFor(dvar_s *dvar)
{
    if (!dvar)
    {
        return nullptr;
    }

    for (std::size_t index = 0; index < g_dvarCount; ++index)
    {
        if (&g_dvarPool[index].dvar == dvar)
        {
            return &g_dvarPool[index];
        }
    }
    return nullptr;
}

dvar_s *FindMutable(const char *name)
{
    if (!name || !*name)
    {
        return nullptr;
    }

    for (dvar_s *dvar = g_dvarHashTable[HashName(name)]; dvar; dvar = dvar->hashNext)
    {
        if (NamesEqual(name, dvar->name))
        {
            return dvar;
        }
    }
    return nullptr;
}

void RefreshStringPointers(DvarStorage &storage)
{
    storage.dvar.name = storage.name.c_str();
    storage.dvar.description = storage.description.c_str();
    storage.dvar.current.string = storage.current.c_str();
    storage.dvar.latched.string = storage.latched.c_str();
    storage.dvar.reset.string = storage.reset.c_str();
}

void GetCombinedString(char *combined, std::size_t combinedSize, int firstArgument)
{
    combined[0] = '\0';
    std::size_t used = 0;

    for (int index = firstArgument; index < Cmd_Argc(); ++index)
    {
        const char *argument = Cmd_Argv(index);
        const std::size_t argumentLength = std::strlen(argument);
        const std::size_t separatorLength = index == firstArgument ? 0 : 1;
        if (used + separatorLength + argumentLength >= combinedSize)
        {
            Com_Printf(0, "dvar value is too long; truncating to %u bytes\n",
                static_cast<unsigned int>(combinedSize - 1));
            break;
        }

        if (separatorLength)
        {
            combined[used++] = ' ';
        }
        std::memcpy(combined + used, argument, argumentLength);
        used += argumentLength;
        combined[used] = '\0';
    }
}

void Dvar_Set_f()
{
    if (Cmd_Argc() < 3)
    {
        Com_Printf(0, "USAGE: set <variable> <value>\n");
        return;
    }

    const char *name = Cmd_Argv(1);
    if (!IsValidName(name))
    {
        Com_Printf(0, "invalid variable name: %s\n", name);
        return;
    }

    char combined[DVAR_COMMAND_LENGTH]{};
    GetCombinedString(combined, sizeof(combined), 2);
    Dvar_SetCommand(name, combined);
}
} // namespace

void Dvar_Init()
{
    if (g_dvarSystemActive)
    {
        return;
    }

    g_dvarSystemActive = true;
    Cmd_AddCommandInternal("set", Dvar_Set_f, &g_setCommand);
}

void Dvar_Shutdown()
{
    Cmd_RemoveCommand("set");
    g_dvarHashTable.fill(nullptr);
    for (std::size_t index = 0; index < g_dvarCount; ++index)
    {
        g_dvarPool[index] = DvarStorage{};
    }
    g_dvarCount = 0;
    g_dvarSystemActive = false;
    g_setCommand = {};
}

bool Dvar_IsSystemActive()
{
    return g_dvarSystemActive;
}

const dvar_s *Dvar_FindVar(const char *name)
{
    return FindMutable(name);
}

const dvar_s *Dvar_RegisterString(
    const char *name,
    const char *value,
    uint16_t flags,
    const char *description)
{
    if (!IsValidName(name) || !value)
    {
        Com_Printf(0, "Dvar_RegisterString: invalid name or value\n");
        return nullptr;
    }

    if (dvar_s *existing = FindMutable(name))
    {
        if (existing->type != DVAR_TYPE_STRING)
        {
            Com_Printf(0, "Dvar_RegisterString: %s has a different type\n", name);
            return nullptr;
        }
        existing->flags |= flags;
        if (description)
        {
            DvarStorage *storage = StorageFor(existing);
            if (!storage)
            {
                Com_Printf(0, "Dvar_RegisterString: storage for '%s' is unavailable\n", name);
                return nullptr;
            }
            storage->description = description;
            RefreshStringPointers(*storage);
        }
        return existing;
    }

    if (g_dvarCount == g_dvarPool.size())
    {
        Com_Printf(0, "Can't create dvar '%s': dvar pool is full\n", name);
        return nullptr;
    }

    DvarStorage &storage = g_dvarPool[g_dvarCount++];
    storage.name = name;
    storage.description = description ? description : "";
    storage.current = value;
    storage.latched = value;
    storage.reset = value;
    storage.dvar.flags = flags;
    storage.dvar.type = DVAR_TYPE_STRING;
    storage.dvar.modified = false;
    RefreshStringPointers(storage);

    const std::size_t hash = HashName(name);
    storage.dvar.hashNext = g_dvarHashTable[hash];
    g_dvarHashTable[hash] = &storage.dvar;
    return &storage.dvar;
}

const char *Dvar_GetString(const char *name)
{
    const dvar_s *dvar = Dvar_FindVar(name);
    if (!dvar || dvar->type != DVAR_TYPE_STRING || !dvar->current.string)
    {
        return "";
    }
    return dvar->current.string;
}

void Dvar_SetCommand(const char *name, const char *value)
{
    if (!IsValidName(name) || !value)
    {
        Com_Printf(0, "invalid variable name: %s\n", name ? name : "<null>");
        return;
    }

    dvar_s *dvar = FindMutable(name);
    if (!dvar)
    {
        Dvar_RegisterString(name, value, DVAR_EXTERNAL, "External Dvar");
        return;
    }
    if (dvar->type != DVAR_TYPE_STRING)
    {
        Com_Printf(0, "dvar '%s' is not string-based\n", name);
        return;
    }
    if (dvar->flags & DVAR_ROM)
    {
        Com_Printf(0, "%s is read only.\n", name);
        return;
    }
    if (dvar->flags & DVAR_INIT)
    {
        Com_Printf(0, "%s is write protected.\n", name);
        return;
    }

    DvarStorage *storage = StorageFor(dvar);
    if (!storage)
    {
        Com_Printf(0, "Dvar_SetCommand: storage for '%s' is unavailable\n", name);
        return;
    }
    storage->current = value;
    storage->latched = value;
    storage->dvar.modified = true;
    RefreshStringPointers(*storage);
}

int Dvar_Command()
{
    if (Cmd_Argc() == 0)
    {
        return 0;
    }

    const char *name = Cmd_Argv(0);
    const dvar_s *dvar = Dvar_FindVar(name);
    if (!dvar)
    {
        return 0;
    }

    if (Cmd_Argc() == 1)
    {
        Com_Printf(
            0,
            "\"%s\" is: \"%s\" default: \"%s\"\n",
            dvar->name,
            dvar->current.string,
            dvar->reset.string);
        return 1;
    }

    char combined[DVAR_COMMAND_LENGTH]{};
    GetCombinedString(combined, sizeof(combined), 1);
    Dvar_SetCommand(name, combined);
    return 1;
}
