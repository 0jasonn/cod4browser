#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <universal/q_shared.h>
#include <client/client.h>

int __cdecl CL_ControllerIndexFromClientNum(int clientIndex)
{
    if (clientIndex)
    {
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\client\\cl_main.cpp",
            230,
            0,
            "clientIndex doesn't index STATIC_MAX_LOCAL_CLIENTS\n\t%i not in [0, %i)",
            clientIndex,
            1);
    }
    return cl_controller_in_use;
}
