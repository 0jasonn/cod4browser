#include <qcommon/qcommon.h>
#include <database/database.h>
#include <bgame/bg_weapons.h>
#include <script/scr_debugger.h>
#include <server/server.h>
#include <universal/com_memory.h>
#include <universal/dvar.h>
#include <xanim/xanim.h>

#include <cstdarg>

int com_expectedHunkUsage;
std::uint32_t com_errorPrintsCount;
const dvar_t *nextmap;
int weaponInfoSource;

void Com_DPrintf(int channel, const char *fmt, ...)
{
    if (!com_developer || !com_developer->current.integer) return;
    char string[4096]{};
    va_list args;
    va_start(args, fmt);
    _vsnprintf(string, sizeof(string), fmt, args);
    va_end(args);
    string[sizeof(string) - 1] = '\0';
    Com_Printf(channel, "%s", string);
}

void __cdecl Com_CheckSyncFrame()
{
    iassert(Sys_IsMainThread());
    SV_WaitSaveGame();
    Scr_UpdateRemoteDebugger();
    DB_Update();
}

void __cdecl Com_SetWeaponInfoMemory(int source)
{
    iassert(source == 1 || source == 2);
    iassert(weaponInfoSource == 0);
    weaponInfoSource = source;
}

void __cdecl Com_FreeWeaponInfoMemory(int source)
{
    iassert(source == 1 || source == 2);
    if (source == weaponInfoSource)
    {
        weaponInfoSource = 0;
        BG_ShutdownWeaponDefFiles();
    }
}

void Com_XAnimFreeSmallTree(XAnimTree_s *animtree)
{
    XAnimFreeTree(animtree, reinterpret_cast<void (*)(void *, int)>(MT_Free));
}

namespace
{
void *Com_AllocSmallAnimTree(int size)
{
    return MT_Alloc(size, MT_TYPE_SMALL_ANIM_TREE);
}
}

XAnimTree_s *Com_XAnimCreateSmallTree(XAnim_s *anims)
{
    return XAnimCreateTree(anims, Com_AllocSmallAnimTree);
}

bool Com_IsRunningMenuLevel()
{
    return com_sv_running->current.enabled &&
        I_strnicmp(sv_mapname->current.string, "menu_", 5) == 0;
}
