#include <universal/com_files.h>

void __cdecl FS_ConvertPath(char *path)
{
    while (*path)
    {
        if (*path == '\\' || *path == ':')
            *path = '/';
        ++path;
    }
}
