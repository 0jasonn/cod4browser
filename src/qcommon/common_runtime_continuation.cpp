// Canonical common.cpp ownership after the temporary Gate 3 Com_Init prefix.
// Keep this translation unit free of browser policy: it exists only because
// KISAK_GATE3_COM_INIT_PREFIX currently excludes the remainder of common.cpp.

#include <qcommon/qcommon.h>
#include <qcommon/cmd.h>
#include <qcommon/com_playerprofile.h>
#include <qcommon/system.h>
#include <qcommon/sys_event_types.h>
#include <client/client.h>
#include <stringed/stringed_hooks.h>
#include <universal/com_memory.h>
#include <universal/dvar.h>
#include <universal/q_parse.h>
#include <script/scr_vm_runtime.h>

#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <algorithm>

int com_fixedConsolePosition;
int com_consoleLogOpenFailed;
int com_missingAssetOpenFailed;
int com_lastFrameTime[4];
int com_fullyInitialized;
float com_timescaleValue = 1.0f;
int com_frameTime;
const dvar_t *com_recommendedSet;
const dvar_t *version;
float com_codeTimeScale = 1.0f;
int com_safemode;

extern int com_numConsoleLines;
extern char *com_consoleLines[32];

void __cdecl Com_RunAutoExec(int localClientNum, int controllerIndex)
{
    Dvar_SetInAutoExec(1);
    Cmd_ExecuteSingleCommand(localClientNum, controllerIndex,
        const_cast<char *>("exec autoexec_dev.cfg"));
    Dvar_SetInAutoExec(0);
}

int __cdecl Com_SafeMode()
{
    for (int index = 0; index < com_numConsoleLines; ++index)
    {
        Cmd_TokenizeString(com_consoleLines[index]);
        const bool safe = !I_stricmp(Cmd_Argv(0), "safe") ||
            !I_stricmp(Cmd_Argv(0), "dvar_restart");
        Cmd_EndTokenizedString();
        if (safe)
        {
            *com_consoleLines[index] = '\0';
            return 1;
        }
    }
    return com_safemode;
}

void __cdecl Com_ExecStartupConfigs(int localClientNum, const char *configFile)
{
    Cbuf_AddText(localClientNum, "exec default.cfg\n");
    Cbuf_AddText(localClientNum, "exec language.cfg\n");
    if (configFile)
        Cbuf_AddText(localClientNum, va("exec %s\n", configFile));

    const int controllerIndex = CL_ControllerIndexFromClientNum(localClientNum);
    Cbuf_Execute(localClientNum, controllerIndex);
    Com_RunAutoExec(localClientNum, controllerIndex);
    if (Com_SafeMode())
        Cbuf_AddText(localClientNum, "exec safemode.cfg\n");
    Cbuf_Execute(localClientNum, controllerIndex);
}

void __cdecl Com_WriteConfiguration(int localClientNum)
{
    char configFile[68];
    if (!com_fullyInitialized || (dvar_modifiedFlags & DVAR_ARCHIVE) == 0)
        return;
    dvar_modifiedFlags &= ~DVAR_ARCHIVE;
    if (!Com_HasPlayerProfile())
        return;
    Com_BuildPlayerProfilePath(configFile, 64, "config.cfg");
    Com_WriteConfigToFile(localClientNum, configFile);
}

double __cdecl Com_GetTimescaleForSnd()
{
    if (com_fixedtime->current.integer)
        return static_cast<double>(com_fixedtime->current.integer);
    return static_cast<double>(
        com_timescale->current.value * dev_timescale->current.value);
}

void __cdecl Field_Clear(field_t *edit)
{
    std::memset(edit->buffer, 0, sizeof(edit->buffer));
    edit->cursor = 0;
    edit->scroll = 0;
    edit->drawWidth = 256;
}

char __cdecl Com_GetDecimalDelimiter()
{
    const int language = loc_language->current.integer;
    return language == 1 || language == 2 || language == 3 ||
        language == 4 || language == 6 || language == 7 ||
        language == 14 ? ',' : '.';
}

void __cdecl Com_LocalizedFloatToString(
    float value, char *buffer, std::uint32_t maxlen,
    std::uint32_t numDecimalPlaces)
{
    _snprintf(buffer, maxlen - 1, "%.*f", numDecimalPlaces, value);
    buffer[maxlen - 1] = '\0';
    const char delimiter = Com_GetDecimalDelimiter();
    if (delimiter == '.') return;
    for (std::uint32_t index = 0; index < maxlen; ++index)
    {
        if (buffer[index] == '.')
        {
            buffer[index] = delimiter;
            return;
        }
    }
}

void __cdecl Com_FreeEvent(char *ptr)
{
    Z_Free(ptr, 10);
}

void __cdecl Com_EventLoop()
{
    sysEvent_t storage{};
    for (;;)
    {
        const sysEvent_t event = *Sys_GetEvent(&storage);
        switch (event.evType)
        {
        case SE_NONE:
            iassert(!event.evPtr);
            return;
        case SE_KEY:
            iassert(!event.evPtr);
            CL_KeyEvent(0, event.evValue, event.evValue2, event.evTime);
            break;
        case SE_CHAR:
            iassert(!event.evPtr);
            CL_CharEvent(0, event.evValue);
            break;
        case SE_CONSOLE:
            iassert(event.evPtr);
            Cbuf_AddText(0, static_cast<const char *>(event.evPtr));
            Com_FreeEvent(static_cast<char *>(event.evPtr));
            Cbuf_AddText(0, "\n");
            break;
        default:
            iassert(!event.evPtr);
            Com_Error(ERR_FATAL, "Com_EventLoop: bad event type %i",
                event.evType);
            break;
        }
    }
}

void __cdecl Com_SetScriptSettings()
{
    Scr_Settings(
        com_developer->current.integer || com_logfile->current.integer,
        com_developer_script->current.integer,
        com_developer_script_abort_on_error->current.integer);
}

void Com_ResetFrametime()
{
    const int now = static_cast<int>(Sys_Milliseconds());
    com_lastFrameTime[0] = now;
    com_lastFrameTime[1] = now;
    com_lastFrameTime[2] = now;
}

void Com_CheckError()
{
    Sys_EnterCriticalSection(CRITSECT_COM_ERROR);
    const int entered = com_errorEntered;
    Sys_LeaveCriticalSection(CRITSECT_COM_ERROR);
    if (entered)
    {
        if (auto *errorBoundary = static_cast<jmp_buf *>(Sys_GetValue(2)))
            longjmp(*errorBoundary, -1);
    }
}

void Com_SetTimeScale(float timescale)
{
    iassert(timescale > 0.0f);
    com_codeTimeScale = timescale;
}

void __cdecl Debug_Frame(int)
{
    // Remote native script-debugger transport is not present in browsers.
    // Preserve the ordinary input/time/sound portion of the debug frame.
    IN_Frame();
    const int now = static_cast<int>(Sys_Milliseconds());
    const int elapsed = std::max(0, now - com_frameTime);
    com_frameTime = now;
    cls.realFrametime = elapsed;
    cls.realtime += elapsed;
    CL_UpdateSound();
}
