#include <qcommon/qcommon.h>
#include <universal/com_memory.h>

std::uint8_t *__cdecl CM_Hunk_Alloc(
    std::uint32_t size, const char *name, int type)
{
    return static_cast<std::uint8_t *>(Hunk_Alloc(size, name, type));
}
