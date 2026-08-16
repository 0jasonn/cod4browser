#pragma once

#include <zlib.h>

// zlib 1.1.4, retained by the native KisakCOD targets, predates the public
// compressBound API. This is the conservative bound used by newer zlib
// releases and keeps synthetic test-fixture compression version-independent.
inline uLongf KisakTestCompressBound(const uLong sourceBytes) noexcept
{
    return static_cast<uLongf>(sourceBytes) + (sourceBytes >> 12u) +
        (sourceBytes >> 14u) + (sourceBytes >> 25u) + 13u;
}
