#pragma once

#include <cstdint>

// Canonical KisakCOD/IW3 animation asset structures. This header deliberately
// contains only the database-facing XAnimParts graph so portable database
// loaders do not need the renderer, DObj, game, or platform include graph from
// xanim.h.

union XAnimIndices // IW3 size: 0x4
{
    std::uint8_t *_1;
    std::uint16_t *_2;
    void *data;
};

struct XAnimNotifyInfo // IW3 size: 0x8
{
    std::uint16_t name;
    std::uint16_t padding;
    float time;
};

union XAnimDynamicIndices // IW3 size: 0x2
{
    std::uint8_t _1[1];
    std::uint16_t _2[1];
};

struct XAnimDeltaPartQuatDataFrames // IW3 size: 0x8
{
    std::int16_t (*frames)[2];
    XAnimDynamicIndices indices;
    std::uint16_t padding;
};

union XAnimDynamicFrames // IW3 size: 0x4
{
    std::uint8_t (*_1)[3];
    std::uint16_t (*_2)[3];
};

struct XAnimPartTransFrames // IW3 size: 0x20
{
    float mins[3];
    float size[3];
    XAnimDynamicFrames frames;
    XAnimDynamicIndices indices;
    std::uint16_t padding;
};

union XAnimPartTransData // IW3 size: 0x20
{
    XAnimPartTransFrames frames;
    float frame0[3];
};

struct XAnimPartTrans // IW3 size: 0x24
{
    std::uint16_t size;
    std::uint8_t smallTrans;
    std::uint8_t padding;
    XAnimPartTransData u;
};

union XAnimDeltaPartQuatData // IW3 size: 0x8
{
    XAnimDeltaPartQuatDataFrames frames;
    std::int16_t frame0[2];
};

union XAnimPartQuatFrames // IW3 size: 0x4
{
    std::int16_t (*frames)[4];
    std::int16_t (*frames2)[2];
};

struct XAnimPartQuatDataFrames // IW3 size: 0x8
{
    XAnimPartQuatFrames u;
    XAnimDynamicIndices indices;
    std::uint16_t padding;
};

union XAnimPartQuatData // IW3 size: 0x8
{
    XAnimPartQuatDataFrames frames;
    std::int16_t frame0[4];
    std::int16_t frame02[2];
};

struct XAnimPartQuat // IW3 size: 0xC
{
    std::uint16_t size;
    std::uint16_t padding;
    XAnimPartQuatData u;
};

struct XAnimPartQuatPtr // IW3 size: 0x8
{
    XAnimPartQuat *quat;
    std::uint8_t partIndex;
    std::uint8_t padding[3];
};

struct XAnimPartTransPtr // IW3 size: 0x8
{
    XAnimPartTrans *trans;
    std::uint8_t partIndex;
    std::uint8_t padding[3];
};

struct XAnimDeltaPartQuat // IW3 size: 0xC
{
    std::uint16_t size;
    std::uint16_t padding;
    XAnimDeltaPartQuatData u;
};

struct XAnimDeltaPart // IW3 size: 0x8
{
    XAnimPartTrans *trans;
    XAnimDeltaPartQuat *quat;
};

struct XAnimTime // IW3 size: 0xC
{
    float time;
    float frameFrac;
    int frameIndex;
};

struct XAnimParts // IW3 size: 0x58
{
    const char *name;
    std::uint16_t dataByteCount;
    std::uint16_t dataShortCount;
    std::uint16_t dataIntCount;
    std::uint16_t randomDataByteCount;
    std::uint16_t randomDataIntCount;
    std::uint16_t numframes;
    bool bLoop;
    bool bDelta;
    std::uint8_t boneCount[10];
    std::uint8_t notifyCount;
    std::uint8_t assetType;
    bool isDefault;
    std::uint8_t padding;
    std::uint32_t randomDataShortCount;
    std::uint32_t indexCount;
    float framerate;
    float frequency;
    std::uint16_t *names;
    std::uint8_t *dataByte;
    std::int16_t *dataShort;
    int *dataInt;
    std::int16_t *randomDataShort;
    std::uint8_t *randomDataByte;
    int *randomDataInt;
    XAnimIndices indices;
    XAnimNotifyInfo *notify;
    XAnimDeltaPart *deltaPart;
};

static_assert(sizeof(void *) != 4u || sizeof(XAnimIndices) == 4u);
static_assert(sizeof(void *) != 4u || sizeof(XAnimNotifyInfo) == 8u);
static_assert(sizeof(void *) != 4u || sizeof(XAnimDynamicIndices) == 2u);
static_assert(sizeof(void *) != 4u || sizeof(XAnimPartTrans) == 36u);
static_assert(sizeof(void *) != 4u || sizeof(XAnimDeltaPartQuat) == 12u);
static_assert(sizeof(void *) != 4u || sizeof(XAnimDeltaPart) == 8u);
static_assert(sizeof(void *) != 4u || sizeof(XAnimParts) == 88u);
