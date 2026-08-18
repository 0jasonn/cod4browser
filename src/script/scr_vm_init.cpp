#include <script/scr_animtree_runtime.h>
#include <script/scr_compile_runtime.h>
#include <script/scr_main.h>
#include <script/scr_variable.h>
#include <script/scr_vm_runtime.h>

#include <qcommon/engine_lifecycle_trace.h>
#include <qcommon/qcommon.h>

#include <csetjmp>
#include <cstdint>

scrVmPub_t scrVmPub{};
scrVmGlob_t scrVmGlob{};
jmp_buf g_script_error[33]{};
scrVmDebugPub_t scrVmDebugPub{};
function_stack_t fs{};

const dvar_s *logScriptTimes = nullptr;

int opcode = 0;
int caseCount = 0;
int thread_count = 0;

scrCompilePub_t scrCompilePub{};
scrAnimPub_t scrAnimPub{};

void __cdecl Scr_ClearErrorMessage()
{
    scrVarPub.error_message = nullptr;
    scrVmGlob.dialog_error_message = nullptr;
    scrVarPub.error_index = 0;
}

void __cdecl Scr_Init()
{
    EmitEngineLifecycleTrace(EngineLifecycleStage::ScriptVmInitBegin);
    if (scrVarPub.bInited)
        MyAssertHandler(".\\script\\scr_vm.cpp", 169, 0, "%s", "!scrVarPub.bInited");
    Scr_InitClassMap();
    Scr_VM_Init();
    scrCompilePub.script_loading = false;
    scrAnimPub.animtree_loading = false;
    scrCompilePub.scripts = 0;
    scrCompilePub.loadedscripts = 0;
    scrAnimPub.animtrees = 0;
    scrCompilePub.builtinMeth = 0;
    scrCompilePub.builtinFunc = 0;
    scrVarPub.bInited = true;
    EmitEngineLifecycleTrace(EngineLifecycleStage::ScriptVmInitComplete);
}

const dvar_s *Scr_VM_Init()
{
    scrVarPub.varUsagePos = "<script init variable>";
    scrVmPub.maxstack = &scrVmPub.stack[2047];
    scrVmPub.top = scrVmPub.stack;
    scrVmPub.function_count = 0;
    scrVmPub.function_frame = scrVmPub.function_frame_start;
    scrVmPub.localVars = reinterpret_cast<std::uint32_t *>(&scrVmGlob.starttime);
    scrVarPub.evaluate = false;
    scrVmPub.debugCode = false;
    Scr_ClearErrorMessage();
    scrVmPub.terminal_error = false;
    scrVmPub.outparamcount = 0;
    scrVmPub.inparamcount = 0;
    scrVarPub.tempVariable = AllocValue();
    scrVarPub.timeArrayId = 0;
    scrVarPub.pauseArrayId = 0;
    scrVarPub.levelId = 0;
    scrVarPub.gameId = 0;
    scrVarPub.animId = 0;
    scrVarPub.freeEntList = 0;
    scrVmPub.stack[0].type = VAR_CODEPOS;
    scrVmGlob.loading = 0;
    scrVmGlob.recordPlace = false;
    scrVmGlob.lastFileName = nullptr;
    scrVmGlob.lastLine = 0;
    scrVarPub.ext_threadcount = 0;
    scrVarPub.numScriptThreads = 0;
    scrVarPub.varUsagePos = nullptr;
    logScriptTimes = Dvar_RegisterBool(
        "logScriptTimes", false, DVAR_NOFLAG,
        "Log times for every print called from script");
    return logScriptTimes;
}

void __cdecl Scr_Settings(int developer, int developerScript, int abortOnError)
{
    scrVarPub.developer = developer != 0;
    scrVarPub.developer_script = developerScript != 0;
    scrVmPub.abort_on_error = abortOnError != 0;
}
