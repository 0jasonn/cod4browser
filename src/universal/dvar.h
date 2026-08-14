#pragma once

#include <universal/q_shared.h>

// This is intentionally narrower than qcommon.h.  It is the first portable
// dvar slice and keeps the existing engine names so native code can migrate to
// it without introducing a second configuration API.
void Dvar_Init();
void Dvar_Shutdown();
bool Dvar_IsSystemActive();

const dvar_s *Dvar_FindVar(const char *name);
const dvar_s *Dvar_RegisterString(
    const char *name,
    const char *value,
    uint16_t flags,
    const char *description);
const char *Dvar_GetString(const char *name);
void Dvar_SetCommand(const char *name, const char *value);
int Dvar_Command();
