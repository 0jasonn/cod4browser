#pragma once

#include <universal/q_shared.h>
#include <script/scr_variable.h>

struct function_stack_t
{
    const char *pos;
    std::uint32_t localId;
    std::uint32_t localVarCount;
    VariableValue *top;
    VariableValue *startTop;
};
static_assert(sizeof(function_stack_t) == 0x14);

struct function_frame_t
{
    function_stack_t fs;
    Vartype_t topType;
};
static_assert(sizeof(function_frame_t) == 0x18);

struct scrVmPub_t
{
    std::uint32_t *localVars;
    VariableValue *maxstack;
    int function_count;
    function_frame_t *function_frame;
    VariableValue *top;
    bool debugCode;
    bool abort_on_error;
    bool terminal_error;
    std::uint32_t inparamcount;
    std::uint32_t outparamcount;
    std::uint32_t breakpointOutparamcount;
    bool showError;
    function_frame_t function_frame_start[32];
    VariableValue stack[2048];
};
static_assert(sizeof(scrVmPub_t) == 0x4328);

struct FuncDebugData
{
    int breakpointCount;
    const char *name;
    int prof;
    int usage;
};
static_assert(sizeof(FuncDebugData) == 0x10);

struct scrVmDebugPub_t
{
    FuncDebugData func_table[1024];
    int checkBreakon;
    int profileEnable[32768];
    int builtInTime;
    const char *jumpbackHistory[128];
    int jumpbackHistoryIndex;
    int dummy;
};
static_assert(sizeof(scrVmDebugPub_t) == 0x24210);

struct scrVmGlob_t
{
    VariableValue eval_stack[2];
    const char *dialog_error_message;
    int loading;
    int starttime;
    std::uint32_t localVarsStack[2048];
    bool recordPlace;
    char *lastFileName;
    int lastLine;
};
static_assert(sizeof(scrVmGlob_t) == 0x2028);

void __cdecl Scr_ClearErrorMessage();
void __cdecl Scr_Init();
const dvar_s *Scr_VM_Init();
void __cdecl Scr_Settings(
    int developer, int developer_script, int abort_on_error);
void __cdecl Scr_TerminalError(const char *error);
void __cdecl Scr_ShutdownSystem(std::uint8_t sys, int complete);
void __cdecl Scr_AddString(const char *value);
void __cdecl Scr_RunCurrentThreads();
void Scr_SetRecordScriptPlace(int on);
void __cdecl Scr_IncTime();
void __cdecl Scr_InitSystem(int sys);
void __cdecl Scr_SetLoading(int loading);

extern scrVmPub_t scrVmPub;
extern scrVmGlob_t scrVmGlob;
extern scrVmDebugPub_t scrVmDebugPub;
extern function_stack_t fs;
extern int opcode;
extern int caseCount;
extern int thread_count;
extern const dvar_s *logScriptTimes;
