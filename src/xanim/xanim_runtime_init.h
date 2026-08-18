#pragma once

#include <universal/q_shared.h>
#include <xanim/xanim_types.h>

#include <cstdint>

struct DObj_s;
struct XAnimTree_s;

struct XAnimParent
{
    std::uint16_t flags;
    std::uint16_t children;
};

struct XAnimState
{
    float currentAnimTime;
    float oldTime;
    std::int16_t cycleCount;
    std::int16_t oldCycleCount;
    float goalTime;
    float goalWeight;
    float weight;
    float rate;
    bool instantWeightChange;
};
static_assert(sizeof(XAnimState) == 0x20);

struct XAnimInfo
{
    std::uint16_t notifyChild;
    std::int16_t notifyIndex;
    std::uint16_t notifyName;
    std::uint16_t notifyType;
    std::uint16_t prev;
    std::uint16_t next;
    std::uint16_t children;
    std::uint16_t parent;
    std::uint16_t animIndex;
    std::uint16_t animToModel;
    bool inuse;
    XAnimTree_s *tree;
    union
    {
        XAnimParts *parts;
        XAnimParent animParent;
    };
    XAnimState state;
};
static_assert(sizeof(XAnimInfo) == 0x40);

struct XAnimNotify_s
{
    const char *name;
    std::uint32_t type;
    float timeFrac;
};
static_assert(sizeof(XAnimNotify_s) == 0xC);

void __cdecl XAnimInit();

extern int g_info_usage;
extern int g_info_high_usage;
extern int g_notifyListSize;
extern std::uint32_t g_endNotetrackName;
extern bool g_anim_developer;
extern XAnimNotify_s g_notifyList[0x80];
extern XAnimInfo g_xAnimInfo[0x1000];
