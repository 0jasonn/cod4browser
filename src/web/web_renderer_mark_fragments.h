#pragma once

#include <cstdint>

enum MarkFragmentsAgainstEnum : std::int32_t;
struct MarkInfo;
struct FxMarkTri;
struct FxMarkPoint;
struct DObj_s;
struct cpose_t;
struct GfxBrushModel;
struct Material;

void __cdecl R_MarkFragments_Begin(MarkInfo *info,
    MarkFragmentsAgainstEnum markAgainst, const float *origin,
    const float (*axis)[3], float radius, const float *viewOffset,
    Material *material);
char __cdecl R_MarkFragments_AddDObj(MarkInfo *info,
    DObj_s *object, cpose_t *pose, std::uint16_t entityIndex);
char __cdecl R_MarkFragments_AddBModel(MarkInfo *info,
    GfxBrushModel *brushModel, cpose_t *pose, std::uint16_t entityIndex);
void __cdecl R_MarkFragments_Go(MarkInfo *info,
    void(__cdecl *callback)(void *, int, FxMarkTri *, int, FxMarkPoint *,
        const float *, const float *),
    void *callbackContext, int maxTris, FxMarkTri *tris,
    int maxPoints, FxMarkPoint *points);
