#pragma once

// Platform-owned path roots used by portable engine code. Native Win32 and
// the browser VFS provide the implementation; callers never import OS headers.
char *__cdecl Sys_Cwd();
const char *__cdecl Sys_DefaultCDPath();
char *__cdecl Sys_DefaultInstallPath();
