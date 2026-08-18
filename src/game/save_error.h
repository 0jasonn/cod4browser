#pragma once

#include <qcommon/qcommon.h>

enum SaveErrorType : __int32
{
    SAVE_ERROR_MISSING_DEVICE = 0x0,
    SAVE_ERROR_CORRUPT_SAVE = 0x1,
};

void G_SaveError(
    errorParm_t code,
    SaveErrorType errorType,
    const char *fmt,
    ...);
