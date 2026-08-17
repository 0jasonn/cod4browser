#pragma once

#include <gfx_d3d/gfx_draw_surf_types.h>

#include <cstdint>

struct MaterialTextureDef;
struct MaterialConstantDef;
struct GfxImage;
struct IDirect3DVertexDeclaration9;
struct IDirect3DVertexShader9;
struct IDirect3DPixelShader9;
enum MaterialTextureSource : std::uint32_t;

struct GfxVertexShaderLoadDef // sizeof=0x8
{
    void *program;
    std::uint16_t programSize;
    std::uint16_t loadForRenderer;
};

struct GfxPixelShaderLoadDef // sizeof=0x8
{
    void *program;
    std::uint16_t programSize;
    std::uint16_t loadForRenderer;
};

struct MaterialStreamRouting // sizeof=0x2
{
    std::uint8_t source;
    std::uint8_t dest;
};

struct MaterialVertexStreamRouting // sizeof=0x60
{
    MaterialStreamRouting data[16];
    IDirect3DVertexDeclaration9 *decl[16];
};

struct MaterialVertexDeclaration // sizeof=0x64
{
    std::uint8_t streamCount;
    bool hasOptionalSource;
    bool isLoaded;
    std::uint8_t padding;
    MaterialVertexStreamRouting routing;
};

struct MaterialVertexShaderProgram // sizeof=0xC
{
    IDirect3DVertexShader9 *vs;
    GfxVertexShaderLoadDef loadDef;
};

struct MaterialVertexShader // sizeof=0x10
{
    const char *name;
    MaterialVertexShaderProgram prog;
};

struct MaterialPixelShaderProgram // sizeof=0xC
{
    IDirect3DPixelShader9 *ps;
    GfxPixelShaderLoadDef loadDef;
};

struct MaterialPixelShader // sizeof=0x10
{
    const char *name;
    MaterialPixelShaderProgram prog;
};

struct MaterialArgumentCodeConst // sizeof=0x4
{
    std::uint16_t index;
    std::uint8_t firstRow;
    std::uint8_t rowCount;
};

union MaterialArgumentDef // sizeof=0x4
{
    const float *literalConst;
    MaterialArgumentCodeConst codeConst;
    MaterialTextureSource codeSampler;
    std::uint32_t nameHash;
};

struct MaterialShaderArgument // sizeof=0x8
{
    std::uint16_t type;
    std::uint16_t dest;
    MaterialArgumentDef u;
};

struct MaterialPass // sizeof=0x14
{
    MaterialVertexDeclaration *vertexDecl;
    MaterialVertexShader *vertexShader;
    MaterialPixelShader *pixelShader;
    std::uint8_t perPrimArgCount;
    std::uint8_t perObjArgCount;
    std::uint8_t stableArgCount;
    std::uint8_t customSamplerFlags;
    MaterialShaderArgument *args;
};

struct MaterialTechnique // serialized header sizeof=0x8, native minimum sizeof=0x1C
{
    const char *name;
    std::uint16_t flags;
    std::uint16_t passCount;
    MaterialPass passArray[1];
};

struct MaterialTechniqueSet // sizeof=0x94
{
    const char *name;
    std::uint8_t worldVertFormat;
    bool hasBeenUploaded;
    std::uint8_t unused[1];
    std::uint8_t padding;
    MaterialTechniqueSet *remappedTechniqueSet;
    MaterialTechnique *techniques[34];
};

struct GfxStateBits // sizeof=0x8
{
    std::uint32_t loadBits[2];
};

struct WaterWritable // sizeof=0x4
{
    float floatTime;
};

struct complex_s // sizeof=0x8
{
    float real;
    float imag;
};

struct water_t // sizeof=0x44
{
    WaterWritable writable;
    complex_s *H0;
    float *wTerm;
    std::int32_t M;
    std::int32_t N;
    float Lx;
    float Lz;
    float gravity;
    float windvel;
    float winddir[2];
    float amplitude;
    float codeConstant[4];
    GfxImage *image;
};

union MaterialTextureDefInfo // sizeof=0x4
{
    GfxImage *image;
    water_t *water;
};

struct MaterialTextureDef // sizeof=0xC
{
    std::uint32_t nameHash;
    char nameStart;
    char nameEnd;
    std::uint8_t samplerState;
    std::uint8_t semantic;
    MaterialTextureDefInfo u;
};

struct MaterialConstantDef // sizeof=0x20
{
    std::uint32_t nameHash;
    char name[12];
    float literal[4];
};

// Canonical KisakCOD/IW3 database-facing material records. Backend shader and
// texture implementations remain in r_material.h.
struct MaterialInfo // sizeof=0x18
{
    const char *name;
    std::uint8_t gameFlags;
    std::uint8_t sortKey;
    std::uint8_t textureAtlasRowCount;
    std::uint8_t textureAtlasColumnCount;
    GfxDrawSurf drawSurf;
    std::uint32_t surfaceTypeBits;
    std::uint16_t hashIndex;
    std::uint16_t padding;
};

struct Material // IW3 size: 0x50
{
    MaterialInfo info;
    std::uint8_t stateBitsEntry[34];
    std::uint8_t textureCount;
    std::uint8_t constantCount;
    std::uint8_t stateBitsCount;
    std::uint8_t stateFlags;
    std::uint8_t cameraRegion;
    std::uint8_t padding;
    MaterialTechniqueSet *techniqueSet;
    MaterialTextureDef *textureTable;
    MaterialConstantDef *constantTable;
    GfxStateBits *stateBitsTable;
#ifdef KISAK_RADIANT
    int surfaceFlags;
    std::uint16_t editorToolFlags;
    std::uint8_t editorUsage;
    std::uint32_t editorLocale;
#endif
};

static_assert(sizeof(void *) != 4u || sizeof(MaterialInfo) == 24u);
static_assert(sizeof(void *) != 4u || sizeof(GfxVertexShaderLoadDef) == 8u);
static_assert(sizeof(void *) != 4u || sizeof(GfxPixelShaderLoadDef) == 8u);
static_assert(sizeof(void *) != 4u || sizeof(MaterialVertexDeclaration) == 100u);
static_assert(sizeof(void *) != 4u || sizeof(MaterialVertexShader) == 16u);
static_assert(sizeof(void *) != 4u || sizeof(MaterialPixelShader) == 16u);
static_assert(sizeof(void *) != 4u || sizeof(MaterialArgumentDef) == 4u);
static_assert(sizeof(void *) != 4u || sizeof(MaterialShaderArgument) == 8u);
static_assert(sizeof(void *) != 4u || sizeof(MaterialPass) == 20u);
static_assert(sizeof(void *) != 4u || sizeof(MaterialTechnique) == 28u);
static_assert(sizeof(void *) != 4u || sizeof(MaterialTechniqueSet) == 148u);
static_assert(sizeof(void *) != 4u || sizeof(GfxStateBits) == 8u);
static_assert(sizeof(void *) != 4u || sizeof(complex_s) == 8u);
static_assert(sizeof(void *) != 4u || sizeof(water_t) == 68u);
static_assert(sizeof(void *) != 4u || sizeof(MaterialTextureDef) == 12u);
static_assert(sizeof(void *) != 4u || sizeof(MaterialConstantDef) == 32u);
#ifdef KISAK_RADIANT
static_assert(sizeof(void *) != 4u || sizeof(Material) == 96u);
#else
static_assert(sizeof(void *) != 4u || sizeof(Material) == 80u);
#endif
