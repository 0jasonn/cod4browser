#include <universal/q_shared.h>
#include <cgame/cg_draw.h>
#include <cgame/cg_main.h>
#include <cgame/cg_servercmds.h>
#include <client/client.h>
#include <gfx_d3d/r_cgame_api.h>
#include <qcommon/cmd.h>
#include <universal/com_sndalias_runtime.h>
#include <sound/snd_alias_types.h>

#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

CmdArgs cmd_args{};
static std::array<const char *, 9> arguments{};
static const char *cullConfig;
static float capturedCull;
static int capturedClient;
static int capturedDuration;
static double capturedStart;
static double capturedEnd;
static int capturedPriority;
static std::array<float, 3> capturedOrigin;
static int capturedMinRange;
static int capturedMaxRange;
static double capturedFalloff;
static int amplifyCalls;
static int errorCalls;
static shellshock_parms_t shellshock{};
static int subtitleCalls;
static int capturedWidth;

const char *__cdecl Cmd_Argv(int index)
{
    return arguments.at(index);
}

const char *__cdecl CL_GetConfigString(int localClientNum, unsigned int index)
{
    assert(localClientNum == 0 && index == CS_CULLDIST);
    return cullConfig;
}

void R_SetCullDist(float distance)
{
    capturedCull = distance;
}

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}

void Com_PrintError(int, const char *format, ...)
{
    // Exercise the diagnostic, not just its call: float bits are not a format pointer.
    char message[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    ++errorCalls;
}

void __cdecl CG_AlterTimescale(int client, int duration, double start, double end)
{
    capturedClient = client;
    capturedDuration = duration;
    capturedStart = start;
    capturedEnd = end;
}

void __cdecl CG_Blur(int client, int duration, double end, BlurTime timeType,
    BlurTime clock, BlurPriority priority)
{
    assert(timeType == BLUR_TIME_RELATIVE && clock == BLUR_TIME_ABSOLUTE);
    assert(priority == BLUR_PRIORITY_SCRIPT);
    capturedClient = client;
    capturedDuration = duration;
    capturedEnd = end;
}

shellshock_parms_t *__cdecl BG_GetShellshockParms(unsigned int index)
{
    assert(index == 2);
    return &shellshock;
}

void __cdecl SND_SetChannelVolumes(int priority, const float *volumes, int duration)
{
    assert(volumes[0] == 0.25f);
    capturedPriority = priority;
    capturedDuration = duration;
}

void __cdecl SND_DeactivateChannelVolumes(int priority, int duration)
{
    capturedPriority = priority;
    capturedDuration = duration;
}

void __cdecl SND_DeactivateEnvironmentEffects(int priority, int duration)
{
    capturedPriority = priority;
    capturedDuration = duration;
}

void SND_Amplify(float *origin, int minRange, int maxRange,
    double minVolume, double maxVolume, double falloff)
{
    capturedOrigin = {origin[0], origin[1], origin[2]};
    capturedMinRange = minRange;
    capturedMaxRange = maxRange;
    capturedStart = minVolume;
    capturedEnd = maxVolume;
    capturedFalloff = falloff;
    ++amplifyCalls;
}

void __cdecl CL_SubtitlePrint(int client, const char *text, int duration, int width)
{
    assert(client == 0 && std::strcmp(text, "Synthetic subtitle") == 0);
    capturedDuration = duration;
    capturedWidth = width;
    ++subtitleCalls;
}

int main()
{
    // Synthetic commands and dvars only, with the production translation units linked.
    // Wasm long double is 16 bytes: reading its first eight bytes as double corrupts 1.0.
    struct ScaleCase { const char *start; const char *end; double expectedStart; double expectedEnd; };
    for (const ScaleCase &scales : {ScaleCase{"1", "1", 1.0, 1.0},
             ScaleCase{"1", "0.25", 1.0, 0.25}, ScaleCase{"0.25", "1", 0.25, 1.0},
             ScaleCase{"0.5", "2", 0.5, 2.0}, ScaleCase{"0.1", "1", 0.1f, 1.0}})
    {
        arguments = {"slow", "750", scales.start, scales.end};
        CG_SlowServerCommand(0);
        assert(capturedClient == 0 && capturedDuration == 750);
        assert(capturedStart == scales.expectedStart);
        assert(capturedEnd == scales.expectedEnd);
    }

    struct CullCase { const char *text; float expected; };
    for (const CullCase &cull : {CullCase{"4096", 4096.0f},
             CullCase{"0", 0.0f}, CullCase{"-1", -1.0f},
             CullCase{"16777217", 16777216.0f}})
    {
        cullConfig = cull.text;
        CG_ParseCullDist(0);
        assert(capturedCull == cull.expected);
    }

    arguments = {"blur", "275", "0.1", "1", "1"};
    CG_BlurServerCommand(0);
    assert(capturedClient == 0 && capturedDuration == 275);
    assert(capturedEnd == static_cast<double>(0.1f));

    struct FadeCase { const char *seconds; int milliseconds; };
    shellshock.sound.channelvolume[0] = 0.25f;
    for (const FadeCase &fade : {FadeCase{"1.25", 1250}, FadeCase{"0.0004", 0},
             FadeCase{"0.0005", 1}, FadeCase{"0.00049999999", 1},
             FadeCase{"0", 0}, FadeCase{"-0.25", 0}})
    {
        cmd_args.argc[0] = 4;
        arguments = {"setchannelvol", "1", "2", fade.seconds};
        CG_SetChannelVolCmd(0);
        assert(capturedPriority == 1 && capturedDuration == fade.milliseconds);

        cmd_args.argc[0] = 3;
        arguments = {"deactivatechannelvol", "1", fade.seconds};
        CG_DeactivateChannelVolCmd();
        assert(capturedPriority == 1 && capturedDuration == fade.milliseconds);

        arguments = {"deactivatereverb", "1", fade.seconds};
        CG_DeactivateReverbCmd();
        assert(capturedPriority == 1 && capturedDuration == fade.milliseconds);
    }

    cmd_args.argc[0] = 9;
    arguments = {"amplify", "1024.5", "-64.25", "0.1", "32", "1024", "0.1", "0.75", "2"};
    CG_ParseAmp();
    assert(amplifyCalls == 1);
    assert((capturedOrigin == std::array<float, 3>{1024.5f, -64.25f, 0.1f}));
    assert(capturedMinRange == 32 && capturedMaxRange == 1024);
    assert(capturedStart == static_cast<double>(0.1f) && capturedEnd == 0.75);
    assert(capturedFalloff == 2.0);
    arguments[8] = "-1";
    CG_ParseAmp();
    arguments[8] = "2";
    arguments[7] = "0.05";
    CG_ParseAmp();
    arguments[6] = "-0.1";
    CG_ParseAmp();
    assert(amplifyCalls == 1 && errorCalls == 3);

    dvar_t minimum{}, standardWidth{}, widescreenWidth{};
    minimum.current.value = 1.25f;
    standardWidth.current.integer = 320;
    widescreenWidth.current.integer = 480;
    cg_subtitleMinTime = &minimum;
    cg_subtitleWidthStandard = &standardWidth;
    cg_subtitleWidthWidescreen = &widescreenWidth;
    snd_alias_t alias{};
    alias.subtitle = "Synthetic subtitle";
    cgsArray[0].viewAspect = 4.0f / 3.0f;
    CG_SubtitleSndLengthNotify(200, &alias);
    assert(subtitleCalls == 1 && capturedDuration == 1250 && capturedWidth == 320);
    cgsArray[0].viewAspect = 16.0f / 9.0f;
    CG_SubtitleSndLengthNotify(1750, &alias);
    assert(subtitleCalls == 2 && capturedDuration == 1750 && capturedWidth == 480);
    minimum.current.value = 0.0015f;
    CG_SubtitleSndLengthNotify(1, &alias);
    assert(subtitleCalls == 3 && capturedDuration == 2);
    CG_SubtitleSndLengthNotify(0, &alias);
    alias.subtitle = nullptr;
    CG_SubtitleSndLengthNotify(100, &alias);
    assert(subtitleCalls == 3);

    std::puts("cgame numeric: timescale, cull, blur, fades, amplify, and subtitle durations passed");
}
