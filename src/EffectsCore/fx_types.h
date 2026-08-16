#pragma once

struct FxElemDef;

// Canonical database-facing FX asset header. Runtime pools and renderer
// primitives remain in fxprimitives.h.
struct FxEffectDef // IW3 size: 0x20
{
    const char *name;
    int flags;
    int totalSize;
    int msecLoopingLife;
    int elemDefCountLooping;
    int elemDefCountOneShot;
    int elemDefCountEmission;
    const FxElemDef *elemDefs;
};

static_assert(sizeof(void *) != 4u || sizeof(FxEffectDef) == 32u);
