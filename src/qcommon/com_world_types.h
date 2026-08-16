#pragma once

#include <cstddef>
#include <cstdint>

// Canonical KisakCOD/IW3 common-world database records.  Keep these
// declarations renderer-free: the database loader, qcommon, portable tests,
// and the Wasm target all consume the same engine objects.

struct ComPrimaryLight // IW3 size: 0x44 (SP/MP same)
{
    std::uint8_t type;
    std::uint8_t canUseShadowMap;
    std::uint8_t exponent;
    std::uint8_t unused;
    float color[3];
    float dir[3];
    float origin[3];
    float radius;
    float cosHalfFovOuter;
    float cosHalfFovInner;
    float cosHalfFovExpanded;
    float rotationLimit;
    float translationLimit;
    const char *defName;
};

struct ComWorld // IW3 size: 0x10 (SP/MP same)
{
    const char *name;
    std::int32_t isInUse;
    std::uint32_t primaryLightCount;
    ComPrimaryLight *primaryLights;
};

static_assert(sizeof(void *) != 4 || sizeof(ComPrimaryLight) == 68);
static_assert(sizeof(void *) != 4 || offsetof(ComPrimaryLight, defName) == 64);
static_assert(sizeof(void *) != 4 || sizeof(ComWorld) == 16);
static_assert(sizeof(void *) != 4 || offsetof(ComWorld, isInUse) == 4);
static_assert(sizeof(void *) != 4 || offsetof(ComWorld, primaryLightCount) == 8);
static_assert(sizeof(void *) != 4 || offsetof(ComWorld, primaryLights) == 12);
