#pragma once

struct Font_s;
struct Material;

void __cdecl R_AddCmdDrawStretchPic(
    float x,
    float y,
    float w,
    float h,
    float s0,
    float t0,
    float s1,
    float t1,
    const float *color,
    Material *material);
void __cdecl R_AddCmdDrawText(
    const char *text,
    int maxChars,
    Font_s *font,
    float x,
    float y,
    float xScale,
    float yScale,
    float rotation,
    const float *color,
    int style);
void __cdecl R_PushRemoteScreenUpdate(int remoteScreenUpdateNesting);
int __cdecl R_PopRemoteScreenUpdate();
