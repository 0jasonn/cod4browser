#include <database/db_initialization.h>

bool g_initializing = false;

void __cdecl DB_SetInitializing(bool inUse)
{
    g_initializing = inUse;
}
