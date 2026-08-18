#pragma once

#include <cstdint>

struct PhysGeomList;
struct XModelCollSurf_s;
struct XSurface;

void DB_LoadGeneratedXSurfaceArray(XSurface *surfaces, std::int32_t count);
void DB_LoadGeneratedXModelCollSurfArray(
    XModelCollSurf_s *surfaces, std::int32_t count);
void DB_LoadGeneratedPhysGeomList(PhysGeomList *list, bool atStreamStart);
