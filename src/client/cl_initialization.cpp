#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <client/client.h>

#include <client/cl_demo.h>
#include <client/cl_input.h>
#include <client/cl_scrn.h>
#include <qcommon/cmd.h>
#include <qcommon/engine_lifecycle_trace.h>
#include <qcommon/qcommon.h>
#include <stringed/stringed_hooks.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>

#include <cfloat>
#include <cstdlib>

void Campaign_RegisterDvars();
void XModelDumpInfo();

extern const dvar_t *cg_drawCrosshair;
extern const dvar_t *cg_subtitles;
extern const dvar_t *cl_yawspeed;
extern const dvar_t *cl_pitchspeed;
extern const dvar_t *cl_anglespeedkey;

clientConnection_t clientConnections[1];
clientUIActive_t clientUIActives[1];
clientActive_t clients[1];
clientStatic_t cls;

const dvar_t *input_invertPitch;
const dvar_t *cl_avidemo;
const dvar_t *cl_testAnimWeight;
const dvar_t *cl_freemoveScale;
const dvar_t *cl_sensitivity;
const dvar_t *cl_forceavidemo;
const dvar_t *m_yaw;
const dvar_t *m_pitch;
const dvar_t *nextdemo;
const dvar_t *cl_freemove;
const dvar_t *cl_showMouseRate;
const dvar_t *takeCoverWarnings;
const dvar_t *m_forward;
const dvar_t *cheat_items_set2;
const dvar_t *cl_mouseAccel;
const dvar_t *cheat_points;
const dvar_t *input_viewSensitivity;
const dvar_t *input_autoAim;
const dvar_t *cl_inGameVideo;
const dvar_t *cl_noprint;
const dvar_t *m_side;
const dvar_t *m_filter;
const dvar_t *cheat_items_set1;
const dvar_t *cl_freelook;
const dvar_t *cl_shownet;
const dvar_s *arcadeScore[19]{};

namespace
{
cmd_function_s s_forwardToServerCommand;
cmd_function_s s_disconnectClientCommand;
cmd_function_s s_disconnectServerCommand;
cmd_function_s s_vidRestartClientCommand;
cmd_function_s s_vidRestartServerCommand;
cmd_function_s s_sndRestartClientCommand;
cmd_function_s s_sndRestartServerCommand;
cmd_function_s s_demoClientCommand;
cmd_function_s s_demoServerCommand;
cmd_function_s s_timeDemoClientCommand;
cmd_function_s s_timeDemoServerCommand;
cmd_function_s s_recordCommand;
cmd_function_s s_stopRecordCommand;
cmd_function_s s_logoCommand;
cmd_function_s s_cinematicCommand;
cmd_function_s s_unskippableCinematicCommand;
cmd_function_s s_pauseCommand;
cmd_function_s s_voidCommand;
cmd_function_s s_startMultiplayerCommand;
cmd_function_s s_shellExecuteCommand;
cmd_function_s s_incAnimWeightCommand;
cmd_function_s s_decAnimWeightCommand;
cmd_function_s s_xmodelDumpInfoCommand;
cmd_function_s s_stopControllerRumblesCommand;
} // namespace

void __cdecl CL_Init(int localClientNum)
{
    char scoreName[80];

    EmitEngineLifecycleTrace(EngineLifecycleStage::ClientInitBegin);
    Com_Printf(14, "----- Client Initialization -----\n");
    std::srand(Sys_MillisecondsRaw());
    Con_Init();
    iassert(localClientNum == 0);
    clientUIActives[0].connectionState = CA_DISCONNECTED;
    cls.realtime = 0;
    EmitEngineLifecycleTrace(EngineLifecycleStage::ClientInputBegin);
    CL_InitInput();
    EmitEngineLifecycleTrace(EngineLifecycleStage::ClientInputEnd);
    cl_noprint = Dvar_RegisterBool("cl_noprint", false, 0, "Print nothing to the console");
    cl_shownet = Dvar_RegisterInt("cl_shownet", 0, -2, 4, 0, "Display network debugging information");
    cl_avidemo = Dvar_RegisterInt("cl_avidemo", 0, 0, 0x7FFFFFFF, 0, "AVI demo frames per second");
    cl_forceavidemo = Dvar_RegisterBool("cl_forceavidemo", false, 0, "Record AVI demo even if client is not active");
    cl_yawspeed = Dvar_RegisterFloat("cl_yawspeed", 140.0f, -FLT_MAX, FLT_MAX, 0, "Max yaw speed in degrees for game pad and keyboard");
    cl_pitchspeed = Dvar_RegisterFloat("cl_pitchspeed", 140.0f, -FLT_MAX, FLT_MAX, 0, "Max pitch speed in degrees for game pad and keyboard");
    cl_anglespeedkey = Dvar_RegisterFloat("cl_anglespeedkey", 1.5f, 0.0f, FLT_MAX, 0, "Multiplier for max angle speed for gamepad and keyboard");
    cl_sensitivity = Dvar_RegisterFloat("sensitivity", 5.0f, 0.01f, 100.0f, 0, "Mouse sensitivity");
    cl_mouseAccel = Dvar_RegisterFloat("cl_mouseAccel", 0.0f, 0.0f, 100.0f, 0, "Mouse acceleration");
    cl_freelook = Dvar_RegisterBool("cl_freelook", true, DVAR_ARCHIVE, "Enable looking with mouse");
    cl_showMouseRate = Dvar_RegisterBool("cl_showmouserate", false, 0, "Print mouse rate debugging information to the console");
    cl_inGameVideo = Dvar_RegisterBool("r_inGameVideo", true, DVAR_SAVED, "Allow in game cinematics");
    m_pitch = Dvar_RegisterFloat("m_pitch", 0.022f, -1.0f, 1.0f, 0, "Default pitch");
    m_yaw = Dvar_RegisterFloat("m_yaw", 0.022f, -1.0f, 1.0f, 0, "Default yaw");
    m_forward = Dvar_RegisterFloat("m_forward", 0.25f, -1.0f, 1.0f, 0, "Forward speed in units per second");
    m_side = Dvar_RegisterFloat("m_side", 0.25f, -1.0f, 1.0f, 0, "Sideways motion in units per second");
    m_filter = Dvar_RegisterBool("m_filter", false, DVAR_SAVED, "Allow mouse movement smoothing");
    cg_drawCrosshair = Dvar_RegisterBool("cg_drawCrosshair", true, DVAR_SAVED, "Turn on weapon crosshair");
    cg_subtitles = Dvar_RegisterBool("cg_subtitles", true, DVAR_SAVED, "Turn on subtitles");
    takeCoverWarnings = Dvar_RegisterInt("takeCoverWarnings", -1, -1, 50, 0x4001u, "Number of times remaining to show the take cover warning (negative value indicates it has yet to be initialized)");
    cheat_points = Dvar_RegisterInt("cheat_points", 0, 0, 0x7FFFFFFF, 0x4001u, "Used by script for keeping track of cheats");
    cheat_items_set1 = Dvar_RegisterInt("cheat_items_set1", 0, 0, 0x7FFFFFFF, 0x4001u, "Used by script for keeping track of cheats");
    cheat_items_set2 = Dvar_RegisterInt("cheat_items_set2", 0, 0, 0x7FFFFFFF, 0x4001u, "Used by script for keeping track of cheats");
    for (int index = 0; index < 19; ++index)
    {
        Com_sprintf(scoreName, 32, "s%d", index);
        arcadeScore[index] = Dvar_RegisterInt(scoreName, 0, 0, 0x7FFFFFFF, 0x4001u, "Used by script for keeping track of arcade scores");
    }
    input_invertPitch = Dvar_RegisterBool("input_invertPitch", false, 0x400u, "Invert gamepad pitch");
    input_viewSensitivity = Dvar_RegisterFloat("input_viewSensitivity", 1.0f, 0.000099999997f, 5.0f, 0, nullptr);
    input_autoAim = Dvar_RegisterBool("input_autoAim", true, 0x400u, "Turn on auto aim for consoles");
    nextmap = Dvar_RegisterString("nextmap", "", 0, "The next map name");
    nextdemo = Dvar_RegisterString("nextdemo", "", 0, "The next demo to play");
    Dvar_RegisterBool("cg_blood", true, DVAR_SAVED, "Show blood");
    Campaign_RegisterDvars();
    iassert(loc_language);
    iassert(loc_translate);
    iassert(loc_warnings);
    iassert(loc_warningsAsErrors);
    Cmd_AddCommandInternal("cmd", CL_ForwardToServer_f, &s_forwardToServerCommand);
    Cmd_AddCommandInternal("disconnect", Cbuf_AddServerText_f, &s_disconnectClientCommand);
    Cmd_AddServerCommandInternal("disconnect", CL_Disconnect_f, &s_disconnectServerCommand);
    Cmd_AddCommandInternal("demo", Cbuf_AddServerText_f, &s_demoClientCommand);
    Cmd_AddServerCommandInternal("demo", CL_PlayDemo_f, &s_demoServerCommand);
    Cmd_AddCommandInternal("timedemo", Cbuf_AddServerText_f, &s_timeDemoClientCommand);
    Cmd_AddServerCommandInternal("timedemo", CL_PlayDemo_f, &s_timeDemoServerCommand);
    Cmd_AddCommandInternal("vid_restart", Cbuf_AddServerText_f, &s_vidRestartClientCommand);
    Cmd_AddServerCommandInternal("vid_restart", CL_Vid_Restart_f, &s_vidRestartServerCommand);
    Cmd_AddCommandInternal("snd_restart", Cbuf_AddServerText_f, &s_sndRestartClientCommand);
    Cmd_AddServerCommandInternal("snd_restart", CL_Snd_Restart_f, &s_sndRestartServerCommand);
    Cmd_SetAutoComplete("demo", "demos", "spd");
    Cmd_SetAutoComplete("timedemo", "demos", "spd");
    Cmd_AddCommandInternal("record", CL_Record_f, &s_recordCommand);
    Cmd_AddCommandInternal("stoprecord", CL_StopRecord_f, &s_stopRecordCommand);
    Cmd_AddCommandInternal("logo", CL_PlayLogo_f, &s_logoCommand);
    Cmd_AddCommandInternal("cinematic", CL_PlayCinematic_f, &s_cinematicCommand);
    Cmd_AddCommandInternal("unskippablecinematic", CL_PlayUnskippableCinematic_f, &s_unskippableCinematicCommand);
    Cmd_SetAutoComplete("cinematic", "video", "wmv");
    Cmd_AddCommandInternal("pause", CL_Pause_f, &s_pauseCommand);
    Cmd_AddCommandInternal("sl", CL_VoidCommand, &s_voidCommand);
    Cmd_AddCommandInternal("startMultiplayer", CL_startMultiplayer_f, &s_startMultiplayerCommand);
    Cmd_AddCommandInternal("shellExecute", CL_ShellExecute_URL_f, &s_shellExecuteCommand);
    Cmd_AddCommandInternal("+incAnimWeight", reinterpret_cast<void(__cdecl *)()>(CL_IncAnimWeight_f), &s_incAnimWeightCommand);
    Cmd_AddCommandInternal("+decAnimWeight", reinterpret_cast<void(__cdecl *)()>(CL_DecAnimWeight_f), &s_decAnimWeightCommand);
    cl_testAnimWeight = Dvar_RegisterFloat("cl_testAnimWeight", 0.0f, 0.0f, 1.0f, 0, "test animation weighting");
    Cmd_AddCommandInternal("modelDumpInfo", XModelDumpInfo, &s_xmodelDumpInfoCommand);
    Cmd_AddCommandInternal("stopControllerRumble", CL_StopControllerRumbles, &s_stopControllerRumblesCommand);
    Com_Printf(14, "----- Initializing Renderer ----\n");
    EmitEngineLifecycleTrace(EngineLifecycleStage::ClientRendererConfigure);
    CL_InitRef();
    SCR_Init();
    Cbuf_Execute(0, cl_controller_in_use);
    clientUIActives[0].isRunning = 1;
    clients[0].usingAds = 0;
    EmitEngineLifecycleTrace(EngineLifecycleStage::ClientInitComplete);
    Com_Printf(14, "----- Client Initialization Complete -----\n");
}
