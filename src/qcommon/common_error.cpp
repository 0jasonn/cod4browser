// Canonical common.cpp error policy, shared by native and browser frame pumps.
#include <universal/q_shared.h>
#include <qcommon/qcommon.h>
#include <qcommon/cmd.h>
#include <qcommon/files.h>
#include <qcommon/com_bsp.h>
#include <qcommon/system.h>
#include <database/database.h>
#include <gfx_d3d/r_debugger_api.h>
#include <client/client.h>
#include <stringed/stringed_hooks.h>
#include <universal/com_memory.h>
#include <universal/com_files.h>
#include <universal/dvar.h>
#include <universal/profile.h>
#include <universal/q_parse.h>
#ifdef KISAK_SP
#include <ui/ui.h>
#else
#include <ui_mp/ui_mp.h>
#include <bgame/bg_local.h>
#endif

extern errorParm_t errorcode;
extern char com_errorMessage[4096];
void R_ComErrorCleanup();
void FX_UnregisterAll();
void SND_ErrorCleanup();
void NET_RestartDebug();

int lastErrorTime;
int errorCount;
const dvar_t *ui_errorMessage;
const dvar_t *ui_errorTitle;

const char *noticeErrors[10] =
{
  "EXE_SERVER_DISCONNECTED",
  "EXE_DISCONNECTED",
  "EXE_SERVERISFULL",
  "XBOXLIVE_SIGNEDOUTOFLIVE",
  "XBOXLIVE_CANTJOINSESSION",
  "XBOXLIVE_MPNOTALLOWED",
  "XBOXLIVE_MUSTLOGIN",
  "MENU_RESETCUSTOMCLASSES",
  "XBOXLIVE_NETCONNECTION",
  ""
}; // idb

void Com_ClearTempMemory()
{
    Hunk_ClearTempMemory();
    Hunk_ClearTempMemoryHigh();
}

void __cdecl Com_SetLocalizedErrorMessage(char* localizedErrorMessage, const char* titleToken)
{
    char* translation; // [esp+0h] [ebp-4h]

    ui_errorMessage = Dvar_RegisterString("com_errorMessage", (char*)"", DVAR_ROM, "Most recent error message");
    ui_errorTitle = Dvar_RegisterString(
        "com_errorTitle",
        (char*)"",
        DVAR_ROM,
        "Title of the most recent error message");
    translation = SEH_LocalizeTextMessage(titleToken, "error message", LOCMSG_NOERR);
    if (translation)
        Dvar_SetString((dvar_s*)ui_errorTitle, translation);
    else
        Dvar_SetString((dvar_s*)ui_errorTitle, (char*)"");
    Dvar_SetString((dvar_s*)ui_errorMessage, localizedErrorMessage);
    if (com_errorMessage != localizedErrorMessage) I_strncpyz(com_errorMessage, localizedErrorMessage, 4096);
}

void __cdecl Com_SetErrorMessage(char* errorMessage)
{
    char* translation; // [esp+0h] [ebp-8h]
    const char* title; // [esp+4h] [ebp-4h]

    iassert( errorMessage );
    iassert( errorMessage[0] );
    if (errorcode == ERR_SERVERDISCONNECT || Com_ErrorIsNotice(errorMessage))
        title = "MENU_NOTICE";
    else
        title = "MENU_ERROR";
    translation = SEH_LocalizeTextMessage(errorMessage, "error message", LOCMSG_NOERR);
    if (!translation)
        translation = errorMessage;
    Com_SetLocalizedErrorMessage(translation, title);
}

char __cdecl Com_ErrorIsNotice(const char* errorMessage)
{
    int i; // [esp+0h] [ebp-4h]

    for (i = 0; *noticeErrors[i]; ++i)
    {
        if (!I_stricmp(noticeErrors[i], errorMessage))
            return 1;
    }
    return 0;
}

bool shouldQuitOnError;
void __cdecl RefreshQuitOnErrorCondition()
{
    bool v0; // [esp+0h] [ebp-4h]

    if (Dvar_IsSystemActive())
    {
        v0 = Dvar_GetBool("QuitOnError") || Dvar_GetInt("r_vc_compile") == 2;
        shouldQuitOnError = v0;
    }
}

bool __cdecl QuitOnError()
{
    RefreshQuitOnErrorCondition();
    return shouldQuitOnError;
}

void Com_ErrorCleanup()
{
    int MenuScreenForError; // eax
    char v1; // [esp+3h] [ebp-1021h]
    char* v2; // [esp+8h] [ebp-101Ch]
    char* v3; // [esp+Ch] [ebp-1018h]
    char* src; // [esp+14h] [ebp-1010h]
    uint32_t v5; // [esp+18h] [ebp-100Ch]
    char finalmsg[4100]; // [esp+1Ch] [ebp-1008h] BYREF

    iassert( Sys_IsMainThread() );
    LargeLocalReset();
    R_PopRemoteScreenUpdate();
    Com_SyncThreads();
#ifdef KISAK_MP
    if (!com_dedicated->current.enabled)
#endif
    {
        R_ComErrorCleanup();
    }
    Cmd_ComErrorCleanup();
    Dvar_SetInAutoExec(0);
    if (IsFastFileLoad())
        DB_Cleanup();
    Com_ClearTempMemory();
    if (!IsFastFileLoad())
        FX_UnregisterAll();
    if (ProfLoad_IsActive())
        ProfLoad_Deactivate();
    Dvar_SetIntByName("cl_paused", 0);
    FS_PureServerSetLoadedIwds((char*)"", (char*)"");
    SEH_UpdateLanguageInfo();
    v3 = com_errorMessage;
    v2 = finalmsg;
    do
    {
        v1 = *v3;
        *v2++ = *v3++;
    } while (v1);
    if (errorcode == ERR_DISCONNECT)
    {
        if (com_errorMessage[0])
        {
            src = SEH_LocalizeTextMessage(com_errorMessage, "error message", LOCMSG_NOERR);
            if (src)
                I_strncpyz(com_errorMessage, src, 4096);
        }
    }
    else
    {
        if (cls.uiStarted && errorcode != ERR_DROP)
        {
            MenuScreenForError = UI_GetMenuScreenForError();
            UI_SetActiveMenu(0, (uiMenuCommand_t)MenuScreenForError);
        }
        Com_SetErrorMessage(com_errorMessage);
    }
    if (fs_debug && fs_debug->current.integer == 2)
        Dvar_SetInt((dvar_s*)fs_debug, 0);
    SND_ErrorCleanup();
    Com_CleanupBsp();
    KISAK_NULLSUB();
    Com_ResetParseSessions();
    CL_FlushDebugServerData();
    CL_UpdateDebugServerData();
    FS_ResetFiles();
    if (errorcode == ERR_DROP)
        Cbuf_Init();
    v5 = Sys_Milliseconds();
    if ((int)(v5 - lastErrorTime) >= 100)
    {
        errorCount = 0;
    }
    else if (++errorCount > 3)
    {
        errorcode = ERR_FATAL;
    }
    lastErrorTime = v5;
    if (errorcode != ERR_SERVERDISCONNECT && errorcode != ERR_DROP && errorcode != ERR_DISCONNECT)
        Sys_Error("%s", com_errorMessage);
    updateScreenCalled = 0;
    if (errorcode == ERR_SERVERDISCONNECT)
    {
        Com_ShutdownInternal("EXE_DISCONNECTEDFROMOWNLISTENSERVER");
    }
    else
    {
        if (errorcode != ERR_DROP && errorcode != ERR_DISCONNECT)
            MyAssertHandler(
                ".\\qcommon\\common.cpp",
                1163,
                0,
                "%s\n\t(errorcode) = %i",
                "(errorcode == ERR_DROP || errorcode == ERR_DISCONNECT)",
                errorcode);
        if (errorcode == ERR_DROP)
        {
            Com_PrintError(16, "********************\nERROR: %s\n********************\n", com_errorMessage);
            if (cls.uiStarted && !com_fixedConsolePosition)
                CL_ConsoleFixPosition();
        }
        else
        {
            Com_Printf(16, "********************\nDisconnecting: %s\n********************\n", com_errorMessage);
        }
        Com_ShutdownInternal(finalmsg);
        if (errorcode == ERR_DROP && QuitOnError())
            Com_Quit_f();
    }
#ifdef KISAK_MP
    bgs = 0;
#endif
    com_fixedConsolePosition = 0;
    NET_RestartDebug();
    com_errorEntered = 0;
}

