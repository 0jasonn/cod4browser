#pragma once

#include <cstdint>

struct XAnim_s;

struct scr_animtree_t
{
    scr_animtree_t() : anims(nullptr) {}
    XAnim_s *anims;
};
static_assert(sizeof(scr_animtree_t) == 0x4);

#define MAX_XANIMTREE_NUM 0x80

struct scrAnimPub_t
{
    std::uint32_t animtrees;
    std::uint32_t animtree_node;
    std::uint32_t animTreeNames;
    scr_animtree_t xanim_lookup[2][MAX_XANIMTREE_NUM];
    std::uint32_t xanim_num[2];
    std::uint32_t animTreeIndex;
    bool animtree_loading;
};
static_assert(sizeof(scrAnimPub_t) == 0x41C);

extern scrAnimPub_t scrAnimPub;
