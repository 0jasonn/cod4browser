#pragma once

struct GfxPlacement // sizeof=0x1C
{
    float quat[4];
    float origin[3];
};

struct GfxScaledPlacement // sizeof=0x20
{
    GfxPlacement base;
    float scale;
};
