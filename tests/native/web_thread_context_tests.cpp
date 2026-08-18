#include <qcommon/system.h>

#include <cassert>
#include <cstdint>

namespace
{
int g_initializedThreadContext = -1;
}

void Com_InitThreadData(int threadContext)
{
    g_initializedThreadContext = threadContext;
}

int main()
{
    Sys_InitializeCriticalSections();
    assert(Sys_GetCpuCount() == 1);
    assert(!Sys_IsMainThread());
    assert(!Sys_IsRenderThread());
    assert(!Sys_IsServerThread());

    Sys_InitMainThread();
    assert(Sys_IsMainThread());
    assert(g_initializedThreadContext == THREAD_CONTEXT_MAIN);

    std::uint32_t values[4] = {1, 2, 3, 4};
    for (int index = 0; index < 4; ++index)
    {
        Sys_SetValue(index, &values[index]);
        assert(Sys_GetValue(index) == &values[index]);
    }
    assert(Sys_GetValue(-1) == nullptr);
    assert(Sys_GetValue(4) == nullptr);

    FastCriticalSection lock{};
    Sys_LockWrite(&lock);
    assert(lock.writeCount == 1);
    Sys_UnlockWrite(&lock);
    assert(lock.writeCount == 0);

    Sys_EnterCriticalSection(CRITSECT_CBUF);
    Sys_LeaveCriticalSection(CRITSECT_CBUF);
    NET_Sleep(0);
    return 0;
}
