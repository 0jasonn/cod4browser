#include <script/scr_vm_runtime.h>

#include <qcommon/qcommon.h>

// Temporary fail-closed diagnostics boundary for the runtime-init slice. The
// full VM owner will replace this once its debugger/dump closure is linked.
void __cdecl Scr_TerminalError(const char *error)
{
    scrVmPub.terminal_error = true;
    Com_Error(ERR_DROP, "%s", error);
}
