#pragma once

#include <universal/q_shared.h>

void __cdecl Sys_Mkdir(const char *path);
BOOL __cdecl Sys_RemoveDirTree(const char *path);
int __cdecl Sys_CountFileList(char **list);
char **__cdecl Sys_ListFiles(const char *directory, const char *extension,
    const char *filter, int *numfiles, int wantsubs);
int __cdecl Sys_DirectoryHasContents(const char *directory);
