#pragma once

#include <cstdint>

struct FxElemDef;
struct Material;
struct XModel;

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

// Canonical database-facing FX element graph. These records are serialized
// directly by db_load.cpp; renderer/runtime state stays in fxprimitives.h.
struct FxFloatRange // IW3 size: 0x8
{
    float base;
    float amplitude;
};

struct FxSpawnDefLooping // IW3 size: 0x8
{
    int intervalMsec;
    int count;
};

struct FxIntRange // IW3 size: 0x8
{
    int base;
    int amplitude;
};

struct FxSpawnDefOneShot // IW3 size: 0x8
{
    FxIntRange count;
};

union FxSpawnDef // IW3 size: 0x8
{
    FxSpawnDefLooping looping;
    FxSpawnDefOneShot oneShot;
};

struct FxElemAtlas // IW3 size: 0x8
{
    std::uint8_t behavior;
    std::uint8_t index;
    std::uint8_t fps;
    std::uint8_t loopCount;
    std::uint8_t colIndexBits;
    std::uint8_t rowIndexBits;
    std::int16_t entryCount;
};

struct FxElemVec3Range // IW3 size: 0x18
{
    float base[3];
    float amplitude[3];
};

struct FxElemVisualState // IW3 size: 0x18
{
    std::uint8_t color[4];
    float rotationDelta;
    float rotationTotal;
    float size[2];
    float scale;
};

struct FxElemVisStateSample // IW3 size: 0x30
{
    FxElemVisualState base;
    FxElemVisualState amplitude;
};

struct FxElemVelStateInFrame // IW3 size: 0x30
{
    FxElemVec3Range velocity;
    FxElemVec3Range totalDelta;
};

struct FxElemVelStateSample // IW3 size: 0x60
{
    FxElemVelStateInFrame local;
    FxElemVelStateInFrame world;
};

union FxEffectDefRef // IW3 size: 0x4
{
    const FxEffectDef *handle;
    const char *name;
};

union FxElemVisuals // IW3 size: 0x4
{
    const void *anonymous;
    Material *material;
    XModel *model;
    FxEffectDefRef effectDef;
    const char *soundName;

    FxElemVisuals() = default;
    FxElemVisuals(Material *mat) : material(mat) {}
};

struct FxElemMarkVisuals // IW3 size: 0x8
{
    Material *materials[2];
};

union FxElemDefVisuals // IW3 size: 0x4
{
    FxElemMarkVisuals *markArray;
    FxElemVisuals *array;
    FxElemVisuals instance;
};

struct FxTrailVertex // IW3 size: 0x14
{
    float pos[2];
    float normal[2];
    float texCoord;
};

struct FxTrailDef // IW3 size: 0x1C
{
    int scrollTimeMsec;
    int repeatDist;
    int splitDist;
    int vertCount;
    FxTrailVertex *verts;
    int indCount;
    std::uint16_t *inds;
};

struct FxElemDef // IW3 size: 0xFC
{
    int flags;
    FxSpawnDef spawn;
    FxFloatRange spawnRange;
    FxFloatRange fadeInRange;
    FxFloatRange fadeOutRange;
    float spawnFrustumCullRadius;
    FxIntRange spawnDelayMsec;
    FxIntRange lifeSpanMsec;
    FxFloatRange spawnOrigin[3];
    FxFloatRange spawnOffsetRadius;
    FxFloatRange spawnOffsetHeight;
    FxFloatRange spawnAngles[3];
    FxFloatRange angularVelocity[3];
    FxFloatRange initialRotation;
    FxFloatRange gravity;
    FxFloatRange reflectionFactor;
    FxElemAtlas atlas;
    std::uint8_t elemType;
    std::uint8_t visualCount;
    std::uint8_t velIntervalCount;
    std::uint8_t visStateIntervalCount;
    FxElemVelStateSample *velSamples;
    FxElemVisStateSample *visSamples;
    FxElemDefVisuals visuals;
    float collMins[3];
    float collMaxs[3];
    FxEffectDefRef effectOnImpact;
    FxEffectDefRef effectOnDeath;
    FxEffectDefRef effectEmitted;
    FxFloatRange emitDist;
    FxFloatRange emitDistVariance;
    FxTrailDef *trailDef;
    std::uint8_t sortOrder;
    std::uint8_t lightingFrac;
    std::uint8_t useItemClip;
    std::uint8_t unused[1];
};

struct FxImpactEntry // IW3 size: 0x84
{
    const FxEffectDef *nonflesh[29];
    const FxEffectDef *flesh[4];
};

struct FxImpactTable // IW3 size: 0x8
{
    const char *name;
    FxImpactEntry *table;
};

static_assert(sizeof(FxFloatRange) == 8u);
static_assert(sizeof(FxSpawnDef) == 8u);
static_assert(sizeof(FxElemAtlas) == 8u);
static_assert(sizeof(FxElemVec3Range) == 24u);
static_assert(sizeof(FxElemVisualState) == 24u);
static_assert(sizeof(FxElemVisStateSample) == 48u);
static_assert(sizeof(FxElemVelStateSample) == 96u);
static_assert(sizeof(FxTrailVertex) == 20u);
static_assert(sizeof(void *) != 4u || sizeof(FxEffectDefRef) == 4u);
static_assert(sizeof(void *) != 4u || sizeof(FxElemVisuals) == 4u);
static_assert(sizeof(void *) != 4u || sizeof(FxElemMarkVisuals) == 8u);
static_assert(sizeof(void *) != 4u || sizeof(FxElemDefVisuals) == 4u);
static_assert(sizeof(void *) != 4u || sizeof(FxTrailDef) == 28u);
static_assert(sizeof(void *) != 4u || sizeof(FxElemDef) == 252u);
static_assert(sizeof(void *) != 4u || sizeof(FxImpactEntry) == 132u);
static_assert(sizeof(void *) != 4u || sizeof(FxImpactTable) == 8u);
