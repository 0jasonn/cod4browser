#pragma once
#include "gfx_image_types.h"

#ifdef KISAK_RADIANT
// CoD4Radiant.exe imageGlobals: Image_Alloc @0x5128b0 and Image_FindExisting @0x513200
// both index imageGlobals[i] with `& 0x7FFF` (32768 entries, 0x20000-byte array; the
// picmip scalar fields begin at imageGlobals+0x20000, verified). The editor browses
// thousands of loose materials, lazily registering one GfxImage per material colormap;
// the open-addressing probe is intentionally unbounded, so it relies on the 32768-slot
// table never filling. kisak's game-engine value (2048, `// lwss add`) is 16x too small
// for the editor and overflows the table -> the linear probe spins forever while scrolling.
#define IMAGE_HASH_TABLE_SIZE 0x8000   // 32768 (idb & 0x7FFF)
#define IMAGE_HASH_TABLE_MASK 0x7FFF
#else
#define IMAGE_HASH_TABLE_SIZE 2048 // lwss add
#define IMAGE_HASH_TABLE_MASK 0x7FF
#endif
struct ImgGlobals //$C12090365A206BC63E0695BF82A7DA9E // sizeof=0x2014
{                                       // ...
    GfxImage *imageHashTable[IMAGE_HASH_TABLE_SIZE];     // ...
    int picmip;                         // ...
    int picmipBump;                     // ...
    int picmipSpec;                     // ...
    CardMemory totalMemory;             // ...
};

extern ImgGlobals imageGlobals;
void R_SetPicmipForMemory(std::uint32_t texMemInMegs, std::uint32_t sysMemInMegs);
void Image_GetPicmip(const GfxImage *image, Picmip *picmip);
void Image_PicmipForSemantic(std::uint8_t semantic, Picmip *picmip);
