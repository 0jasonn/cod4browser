#include <qcommon/cmd.h>
#include <qcommon/com_init_trace.h>
#include <qcommon/common_api.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <database/db_initialization.h>
#include <script/scr_stringlist.h>
#include <universal/com_constantconfigstrings.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>
#include <universal/physicalmemory.h>

#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstring>

int com_errorEntered;
errorParm_t errorcode;
char com_errorMessage[4096];
int com_numConsoleLines;
char *com_consoleLines[32];

const dvar_t *com_hiDef;
const dvar_t *com_animCheck;
const dvar_t *com_developer_script;
const dvar_t *com_developer_script_abort_on_error;
const dvar_t *dev_timescale;
const dvar_t *com_maxfps;
const dvar_t *sv_paused;
const dvar_t *com_fixedtime;
const dvar_t *com_logfile;
const dvar_t *cl_paused;
const dvar_t *com_timescale;
const dvar_t *com_sv_running;
const dvar_t *com_maxFrameTime;
const dvar_t *com_statmon;
const dvar_t *com_filter_output;
const dvar_t *com_developer;
const dvar_t *com_introPlayed;
const dvar_t *com_wideScreen;
const dvar_t *cl_paused_simple;
const dvar_t *useFastFile;
const dvar_t *sys_lockThreads;
const dvar_t *sys_smp_allowed;
static const char *comInitAllocName = "$init";

namespace
{
const char *s_lockThreadNames[4] = {"none", "minimal", "all"};

void TraceCommand(const char *name)
{
    Com_InitTraceCommand(name);
}

void Gate3_ReachComInitXAssetsBoundary()
{
    // The canonical body calls DB_InitThread. A browser database runtime must
    // live in the same dedicated Worker as the synchronous engine filesystem;
    // this checkpoint stops before inventing a DOM-thread substitute.
    Com_InitTraceStage("Com_InitXAssets");
}

void TraceDvar(const char *name)
{
    Com_InitTraceDvar(name);
}
} // namespace

void QDECL Com_PrintMessage(int, const char *message, int)
{
    Sys_Print(message);
}

void QDECL Com_Printf(int channel, const char *format, ...)
{
    char message[4100]{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, 4096, format, arguments);
    va_end(arguments);
    message[4095] = '\0';
    Com_PrintMessage(channel, message, 0);
}

void Com_PrintWarning(int channel, const char *format, ...)
{
    char message[4096]{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    Com_PrintMessage(channel, message, 2);
}

void Com_PrintError(int channel, const char *format, ...)
{
    char message[4096]{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    Com_PrintMessage(channel, message, 1);
}

void _copyDWord(std::uint32_t *destination, std::uint32_t value, std::uint32_t count)
{
    for (std::uint32_t index = 0; index < count; ++index)
    {
        destination[index] = value;
    }
}

bool __cdecl Com_LogFileOpen()
{
    // Filesystem initialization is deliberately beyond this checkpoint.
    return false;
}

void QDECL Com_Error(errorParm_t code, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(com_errorMessage, sizeof(com_errorMessage), format, arguments);
    va_end(arguments);
    com_errorMessage[sizeof(com_errorMessage) - 1] = '\0';
    errorcode = code;
    com_errorEntered = 1;
    if (auto *errorBoundary = static_cast<jmp_buf *>(Sys_GetValue(2)))
    {
        longjmp(*errorBoundary, 1);
    }
    Sys_Error("Com_Error without an initialized qcommon error boundary: %s", com_errorMessage);
}

void __cdecl Com_ParseCommandLine(char *commandLine)
{
    iassert(commandLine);
    com_consoleLines[0] = commandLine;
    com_numConsoleLines = 1;
    while (*commandLine)
    {
        if (*commandLine == '+' || *commandLine == '\n')
        {
            if (com_numConsoleLines == 32)
            {
                return;
            }
            com_consoleLines[com_numConsoleLines++] = commandLine + 1;
            *commandLine = '\0';
        }
        ++commandLine;
    }
}

void __cdecl Com_StartupVariable(const char *match)
{
    for (int lineIndex = 0; lineIndex < com_numConsoleLines; ++lineIndex)
    {
        Cmd_TokenizeString(com_consoleLines[lineIndex]);
        if (!match || !std::strcmp(Cmd_Argv(1), match))
        {
            if (!I_stricmp(Cmd_Argv(0), "set"))
            {
                Dvar_Set_f();
            }
            else if (!I_stricmp(Cmd_Argv(0), "seta"))
            {
                Dvar_SetA_f();
            }
        }
        Cmd_EndTokenizedString();
    }
}

void Com_InitDvars()
{
    com_maxfps = Dvar_RegisterInt("com_maxfps", 0, 0, 1000, DVAR_ARCHIVE, "Cap frames per second");
    TraceDvar("com_maxfps");
    useFastFile = Dvar_RegisterBool("useFastFile", true, DVAR_INIT, "Enables loading data from fast files. Only tools can run without fast files.");
    TraceDvar("useFastFile");
    sys_lockThreads = Dvar_RegisterEnum("sys_lockThreads", s_lockThreadNames, 0, DVAR_NOFLAG, "Prevents specified threads from changing CPUs; improves profiling and may fix bugs, but can hurt performance");
    TraceDvar("sys_lockThreads");
    sys_smp_allowed = Dvar_RegisterBool("sys_smp_allowed", Sys_GetCpuCount() > 1u, DVAR_INIT, "Allow multi-threading");
    TraceDvar("sys_smp_allowed");
    com_developer = Dvar_RegisterInt("developer", 0, 0, 2, DVAR_NOFLAG, "Enable development options");
    TraceDvar("developer");
    com_developer_script = Dvar_RegisterBool("developer_script", false, DVAR_NOFLAG, "Enable developer script comments");
    TraceDvar("developer_script");
    com_developer_script_abort_on_error = Dvar_RegisterBool("developer_script_abort_on_error", false, DVAR_NOFLAG, "Halt Execution when an error is found in the scripts (Retail does not do this)");
    TraceDvar("developer_script_abort_on_error");
    com_logfile = Dvar_RegisterInt("logfile", 1, 0, 2, DVAR_NOFLAG, "Write to log file - 0 = disabled, 1 = async file write, 2 = Sync every write");
    TraceDvar("logfile");
    com_statmon = Dvar_RegisterBool("com_statmon", false, DVAR_NOFLAG, "Draw stats monitor");
    TraceDvar("com_statmon");
    com_timescale = Dvar_RegisterFloat("com_timescale", 1.0f, 0.001f, 1000.0f, DVAR_SAVED | DVAR_CHEAT | DVAR_ROM | DVAR_SYSTEMINFO, "Scale time of each frame");
    TraceDvar("com_timescale");
    dev_timescale = Dvar_RegisterFloat("timescale", 1.0f, 0.001f, 1000.0f, DVAR_CHEAT | DVAR_SYSTEMINFO, "Scale time of each frame");
    TraceDvar("timescale");
    com_fixedtime = Dvar_RegisterInt("fixedtime", 0, 0, 1000, 0x80u, "Use a fixed time rate of each frame");
    TraceDvar("fixedtime");
    com_maxFrameTime = Dvar_RegisterInt("com_maxFrameTime", 100, 50, 5000, DVAR_NOFLAG, "Time slows down if a frame takes longer than this many milliseconds");
    TraceDvar("com_maxFrameTime");
    sv_paused = Dvar_RegisterInt("sv_paused", 0, 0, 2, DVAR_ROM, "Pause the server");
    TraceDvar("sv_paused");
    cl_paused = Dvar_RegisterInt("cl_paused", 0, 0, 2, DVAR_ROM, "Pause the client");
    TraceDvar("cl_paused");
    cl_paused_simple = Dvar_RegisterBool("cl_paused_simple", false, DVAR_NOFLAG, "Toggling pause won't do any additional special processing if true.");
    TraceDvar("cl_paused_simple");
    com_sv_running = Dvar_RegisterBool("sv_running", false, DVAR_ROM, "Server is running");
    TraceDvar("sv_running");
    com_filter_output = Dvar_RegisterBool("com_filter_output", false, DVAR_NOFLAG, "Use console filters for filtering output.");
    TraceDvar("com_filter_output");
    com_introPlayed = Dvar_RegisterBool("com_introPlayed", false, DVAR_ARCHIVE, "Intro movie has been played");
    TraceDvar("com_introPlayed");
    com_animCheck = Dvar_RegisterBool("com_animCheck", false, DVAR_NOFLAG, "Check anim tree");
    TraceDvar("com_animCheck");
    com_hiDef = Dvar_RegisterBool("hiDef", true, DVAR_ROM, "True if the game video is running in high-def.");
    TraceDvar("hiDef");
    com_wideScreen = Dvar_RegisterBool("wideScreen", true, DVAR_ROM, "True if the game video is running in 16x9 aspect, false if 4x3.");
    TraceDvar("wideScreen");
}

void __cdecl Com_Init_Try_Block_Function(char *commandLine)
{
    Com_InitTraceReset();
    Com_InitTraceStage("Com_Init entered");
    Com_Printf(16, "%s %s build %s %s\n", "KisakCoD4", "1.0", CPUSTRING, __DATE__);

    Com_InitTraceStage("Com_ParseCommandLine");
    Com_ParseCommandLine(commandLine);
    Com_InitTraceStage("SL_Init");
    SL_Init();
    Com_InitTraceStage("Swap_Init");
    Swap_Init();
    Com_InitTraceStage("Cbuf_Init");
    Cbuf_Init();
    Com_InitTraceStage("Cmd_Init");
    Cmd_Init();
    Com_InitTraceStage("Com_StartupVariable");
    Com_StartupVariable(nullptr);
    Com_InitTraceStage("Com_InitDvars");
    Com_InitDvars();
    Com_InitTraceStage("CCS_InitConstantConfigStrings");
    CCS_InitConstantConfigStrings();

    Com_InitTraceStage("PMem_Init");
    PMem_Init();
    Com_InitTraceStage("DB_SetInitializing");
    DB_SetInitializing(true);
    Com_InitTraceStage("PMem_BeginAlloc");
    PMem_BeginAlloc(comInitAllocName, 1u);

    auto &trace = const_cast<ComInitTraceSnapshot &>(Com_GetInitTrace());
    trace.commandCount = 0;
    Cmd_ForEach(TraceCommand);
    Com_InitTraceSetCounts(static_cast<std::size_t>(com_numConsoleLines), trace.commandCount);
    const PhysicalMemory *memory = PMem_GetState();
    Com_InitTraceSetMemory(
        static_cast<std::size_t>(memory->prim[1].pos),
        static_cast<std::size_t>(memory->prim[0].pos),
        static_cast<std::size_t>(memory->prim[1].pos),
        static_cast<std::size_t>(memory->prim[1].allocListCount),
        true);
    Gate3_ReachComInitXAssetsBoundary();
    Com_InitTraceStop("DB_InitThread/WorkerHostedDatabase");
}

void __cdecl Com_Init(char *commandLine)
{
    auto *errorBoundary = static_cast<jmp_buf *>(Sys_GetValue(2));
    if (!errorBoundary)
    {
        Sys_Error("Com_Init requires Sys_GetValue(2) error-boundary storage");
        return;
    }
    if (setjmp(*errorBoundary))
    {
        Com_InitTraceStop("error");
        Sys_Error("Error during initialization: %s", com_errorMessage);
        return;
    }
    Com_Init_Try_Block_Function(commandLine);
}
