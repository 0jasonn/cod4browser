#include <universal/q_shared.h>
#include <server/server.h>
#include <server/sv_public.h>
#include <server/sv_game.h>
#include <client/client.h>
#include <game/savememory.h>

// Native SP shutdown, shared with the runtime map/DB target.
void __cdecl SV_Shutdown(const char *finalmsg)
{
    if (com_sv_running && com_sv_running->current.enabled)
    {
        Com_Printf(15, "----- Server Shutdown -----\n");
        SV_RemoveOperatorCommands();
        SV_ShutdownGameProgs();
        SaveMemory_CleanupSaveMemory();
        SaveMemory_ShutdownSaveSystem();
        SV_ClearServer();
        serverStatic_t *cursor = &svs;
        for (int index = 0; index < 10; ++index)
        {
            cursor->initialized = 0;
            cursor = reinterpret_cast<serverStatic_t *>(
                reinterpret_cast<char *>(cursor) + 4);
        }
        Dvar_SetBool(com_sv_running, 0);
        Dvar_SetFloat(com_timescale, 1.0);
        Com_Printf(15, "---------------------------\n");
        CL_Disconnect(0);
    }
}
