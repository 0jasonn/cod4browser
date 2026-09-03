#include <qcommon/qcommon.h>
#include <client/client.h>
#include <database/database.h>
#ifdef KISAK_SP
#include <ui/ui.h>
#include <server/server.h>
#include <server/sv_public.h>
#else
#include <client_mp/client_mp.h>
#include <ui_mp/ui_mp.h>
#include <qcommon/net_chan_mp.h>
#include <server_mp/server_mp.h>
#endif

void __cdecl CL_ShutdownDemo();
void __cdecl R_BeginRemoteScreenUpdate();
void __cdecl R_EndRemoteScreenUpdate();

// Shared native shutdown order, also used when the browser frame pump unwinds
// a canonical disconnect. Quitting the browser Worker is a separate boundary.
void __cdecl Com_ShutdownInternal(const char *finalmsg)
{
    for (int localClientNum = 0; localClientNum < 1; ++localClientNum)
        CL_Disconnect(localClientNum);
    CL_ShutdownAll(false);
    CL_ShutdownDemo();
#ifdef KISAK_MP
    FakeLag_Shutdown();
#endif
    SV_Shutdown(finalmsg);
    Com_Restart();
}

void __cdecl Com_AssetLoadUI()
{
    if (IsFastFileLoad())
    {
        XZoneInfo zoneInfo;
#ifdef KISAK_MP
        zoneInfo.name = "ui_mp";
#else
        zoneInfo.name = "ui";
#endif
        zoneInfo.allocFlags = 8;
        zoneInfo.freeFlags = 104;
        DB_LoadXAssets(&zoneInfo, 1u, 0);
    }
#ifdef KISAK_MP
    UI_SetMap((char *)"", (char *)"");
#else
    UI_SetMap("");
#endif
    R_BeginRemoteScreenUpdate();
    CL_StartHunkUsers();
    R_EndRemoteScreenUpdate();
}
