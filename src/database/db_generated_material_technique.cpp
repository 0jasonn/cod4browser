#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_generated_material_platform.h>
#include <database/db_runtime_prefix.h>

#include <cstddef>
#include <cstdint>
#include <limits>

MaterialTechniqueSet *varMaterialTechniqueSet = nullptr;
MaterialTechniqueSet **varMaterialTechniqueSetPtr = nullptr;
MaterialTechnique *varMaterialTechnique = nullptr;
MaterialTechnique **varMaterialTechniquePtr = nullptr;
MaterialPass *varMaterialPass = nullptr;
MaterialVertexDeclaration *varMaterialVertexDeclaration = nullptr;
MaterialVertexShader *varMaterialVertexShader = nullptr;
MaterialVertexShader **varMaterialVertexShaderPtr = nullptr;
MaterialVertexShaderProgram *varMaterialVertexShaderProgram = nullptr;
GfxVertexShaderLoadDef *varGfxVertexShaderLoadDef = nullptr;
MaterialPixelShader *varMaterialPixelShader = nullptr;
MaterialPixelShader **varMaterialPixelShaderPtr = nullptr;
MaterialPixelShaderProgram *varMaterialPixelShaderProgram = nullptr;
GfxPixelShaderLoadDef *varGfxPixelShaderLoadDef = nullptr;
MaterialShaderArgument *varMaterialShaderArgument = nullptr;
MaterialArgumentDef *varMaterialArgumentDef = nullptr;
MaterialArgumentCodeConst *varMaterialArgumentCodeConst = nullptr;
std::uint32_t *varDWORD = nullptr;
float *varfloat = nullptr;
std::uint32_t *varuint = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical MaterialTechniqueSet loader requires the IW3 32-bit ABI");
static_assert(sizeof(MaterialTechniqueSet) == 148u);
static_assert(offsetof(MaterialTechnique, passArray) == 8u);
static_assert(sizeof(MaterialPass) == 20u);
static_assert(sizeof(MaterialVertexDeclaration) == 100u);
static_assert(sizeof(MaterialVertexShader) == 16u);
static_assert(sizeof(MaterialPixelShader) == 16u);
static_assert(sizeof(MaterialShaderArgument) == 8u);

bool CheckedInlineBytes(std::size_t count, std::size_t stride,
    const char *failureStage, std::size_t &bytes)
{
    if (count > (std::numeric_limits<std::uint32_t>::max)() / stride)
    {
        DB_RuntimeGeneratedFailure(failureStage);
        return false;
    }
    bytes = count * stride;
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure(failureStage);
        return false;
    }
    return true;
}

void Load_DWORDArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (count < 0 || !CheckedInlineBytes(static_cast<std::size_t>(count),
        sizeof(std::uint32_t), "MaterialShader/program array", bytes)) return;
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varDWORD),
        static_cast<std::int32_t>(bytes));
}

void Load_floatArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (count < 0 || !CheckedInlineBytes(static_cast<std::size_t>(count),
        sizeof(float), "MaterialShader/literal array", bytes)) return;
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varfloat),
        static_cast<std::int32_t>(bytes));
}

void Load_GfxVertexShaderLoadDef(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varGfxVertexShaderLoadDef), 8);
    if (DB_RuntimeGeneratedLoadFailed() ||
        !varGfxVertexShaderLoadDef->program) return;
    varGfxVertexShaderLoadDef->program = AllocLoad_FxElemVisStateSample();
    varDWORD = static_cast<std::uint32_t *>(
        varGfxVertexShaderLoadDef->program);
    Load_DWORDArray(true, varGfxVertexShaderLoadDef->programSize);
}

void Load_GfxPixelShaderLoadDef(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varGfxPixelShaderLoadDef), 8);
    if (DB_RuntimeGeneratedLoadFailed() ||
        !varGfxPixelShaderLoadDef->program) return;
    varGfxPixelShaderLoadDef->program = AllocLoad_FxElemVisStateSample();
    varDWORD = static_cast<std::uint32_t *>(
        varGfxPixelShaderLoadDef->program);
    Load_DWORDArray(true, varGfxPixelShaderLoadDef->programSize);
}

void Load_MaterialVertexShaderProgram(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialVertexShaderProgram), 12);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varGfxVertexShaderLoadDef = &varMaterialVertexShaderProgram->loadDef;
    Load_GfxVertexShaderLoadDef(false);
    if (!DB_RuntimeGeneratedLoadFailed())
        Load_CreateMaterialVertexShader(
            &varMaterialVertexShaderProgram->loadDef, varMaterialVertexShader);
}

void Load_MaterialPixelShaderProgram(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialPixelShaderProgram), 12);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varGfxPixelShaderLoadDef = &varMaterialPixelShaderProgram->loadDef;
    Load_GfxPixelShaderLoadDef(false);
    if (!DB_RuntimeGeneratedLoadFailed())
        Load_CreateMaterialPixelShader(
            &varMaterialPixelShaderProgram->loadDef, varMaterialPixelShader);
}

void Load_MaterialVertexShader(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialVertexShader), 16);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &varMaterialVertexShader->name;
    Load_XString(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varMaterialVertexShaderProgram = &varMaterialVertexShader->prog;
    Load_MaterialVertexShaderProgram(false);
}

void Load_MaterialVertexShaderPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialVertexShaderPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed() || !*varMaterialVertexShaderPtr) return;
    if (reinterpret_cast<std::uintptr_t>(*varMaterialVertexShaderPtr) ==
        UINT32_MAX)
    {
        *varMaterialVertexShaderPtr =
            reinterpret_cast<MaterialVertexShader *>(
                AllocLoad_FxElemVisStateSample());
        varMaterialVertexShader = *varMaterialVertexShaderPtr;
        Load_MaterialVertexShader(true);
    }
    else
    {
        DB_ConvertOffsetToPointer(
            reinterpret_cast<std::uint32_t *>(varMaterialVertexShaderPtr));
    }
}

void Load_MaterialPixelShader(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialPixelShader), 16);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &varMaterialPixelShader->name;
    Load_XString(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varMaterialPixelShaderProgram = &varMaterialPixelShader->prog;
    Load_MaterialPixelShaderProgram(false);
}

void Load_MaterialPixelShaderPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialPixelShaderPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed() || !*varMaterialPixelShaderPtr) return;
    if (reinterpret_cast<std::uintptr_t>(*varMaterialPixelShaderPtr) ==
        UINT32_MAX)
    {
        *varMaterialPixelShaderPtr = reinterpret_cast<MaterialPixelShader *>(
            AllocLoad_FxElemVisStateSample());
        varMaterialPixelShader = *varMaterialPixelShaderPtr;
        Load_MaterialPixelShader(true);
    }
    else
    {
        DB_ConvertOffsetToPointer(
            reinterpret_cast<std::uint32_t *>(varMaterialPixelShaderPtr));
    }
}

void Load_MaterialVertexDeclaration(bool atStreamStart)
{
    Load_Stream(atStreamStart, &varMaterialVertexDeclaration->streamCount,
        sizeof(MaterialVertexDeclaration));
}

void Load_MaterialArgumentCodeConst(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialArgumentCodeConst), 4);
}

void Load_MaterialArgumentDef(bool atStreamStart)
{
    switch (varMaterialShaderArgument->type)
    {
    case 1u:
    case 7u:
        if (varMaterialArgumentDef->literalConst)
        {
            if (reinterpret_cast<std::uintptr_t>(
                varMaterialArgumentDef->literalConst) == UINT32_MAX)
            {
                varMaterialArgumentDef->literalConst =
                    reinterpret_cast<const float *>(
                        AllocLoad_FxElemVisStateSample());
                varfloat = const_cast<float *>(
                    varMaterialArgumentDef->literalConst);
                Load_floatArray(true, 4);
            }
            else
            {
                DB_ConvertOffsetToPointer(
                    reinterpret_cast<std::uint32_t *>(varMaterialArgumentDef));
            }
        }
        break;
    case 3u:
    case 5u:
        if (atStreamStart)
        {
            varMaterialArgumentCodeConst =
                reinterpret_cast<MaterialArgumentCodeConst *>(
                    varMaterialArgumentDef);
            Load_MaterialArgumentCodeConst(true);
        }
        break;
    default:
        if (atStreamStart)
        {
            varuint = reinterpret_cast<std::uint32_t *>(varMaterialArgumentDef);
            Load_Stream(true, reinterpret_cast<std::uint8_t *>(varuint), 4);
        }
        break;
    }
}

void Load_MaterialShaderArgument(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialShaderArgument), 8);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varMaterialArgumentDef = &varMaterialShaderArgument->u;
    Load_MaterialArgumentDef(false);
}

void Load_MaterialShaderArgumentArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (count < 0 || !CheckedInlineBytes(static_cast<std::size_t>(count),
        sizeof(MaterialShaderArgument), "MaterialPass/argument array", bytes))
        return;
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialShaderArgument),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    MaterialShaderArgument *entry = varMaterialShaderArgument;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varMaterialShaderArgument = entry;
        Load_MaterialShaderArgument(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_MaterialPass(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varMaterialPass),
        20);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (varMaterialPass->vertexDecl)
    {
        if (reinterpret_cast<std::uintptr_t>(varMaterialPass->vertexDecl) ==
            UINT32_MAX)
        {
            varMaterialPass->vertexDecl =
                reinterpret_cast<MaterialVertexDeclaration *>(
                    AllocLoad_FxElemVisStateSample());
            varMaterialVertexDeclaration = varMaterialPass->vertexDecl;
            Load_MaterialVertexDeclaration(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_BuildVertexDecl(&varMaterialPass->vertexDecl);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                reinterpret_cast<std::uint32_t *>(varMaterialPass));
        }
    }
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varMaterialVertexShaderPtr = &varMaterialPass->vertexShader;
    Load_MaterialVertexShaderPtr(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varMaterialPixelShaderPtr = &varMaterialPass->pixelShader;
    Load_MaterialPixelShaderPtr(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (varMaterialPass->args)
    {
        varMaterialPass->args = reinterpret_cast<MaterialShaderArgument *>(
            AllocLoad_FxElemVisStateSample());
        varMaterialShaderArgument = varMaterialPass->args;
        const std::int32_t count = varMaterialPass->stableArgCount +
            varMaterialPass->perObjArgCount + varMaterialPass->perPrimArgCount;
        Load_MaterialShaderArgumentArray(true, count);
    }
}

void Load_MaterialPassArray(bool atStreamStart, std::int32_t count)
{
    std::size_t bytes = 0;
    if (count < 0 || !CheckedInlineBytes(static_cast<std::size_t>(count),
        sizeof(MaterialPass), "MaterialTechnique/pass array", bytes)) return;
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varMaterialPass),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    MaterialPass *entry = varMaterialPass;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varMaterialPass = entry;
        Load_MaterialPass(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_MaterialTechnique(bool atStreamStart)
{
    if (!atStreamStart)
    {
        DB_RuntimeGeneratedFailure("Load_MaterialTechnique/expected stream start");
        return;
    }
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(varMaterialTechnique), 8);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    iassert(DB_GetStreamPos() ==
        reinterpret_cast<std::uint8_t *>(varMaterialTechnique->passArray));
    varMaterialPass = varMaterialTechnique->passArray;
    Load_MaterialPassArray(true, varMaterialTechnique->passCount);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &varMaterialTechnique->name;
    Load_XString(false);
}

void Load_MaterialTechniquePtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialTechniquePtr), 4);
    if (DB_RuntimeGeneratedLoadFailed() || !*varMaterialTechniquePtr) return;
    if (reinterpret_cast<std::uintptr_t>(*varMaterialTechniquePtr) == UINT32_MAX)
    {
        *varMaterialTechniquePtr = reinterpret_cast<MaterialTechnique *>(
            AllocLoad_FxElemVisStateSample());
        varMaterialTechnique = *varMaterialTechniquePtr;
        Load_MaterialTechnique(true);
    }
    else
    {
        DB_ConvertOffsetToPointer(
            reinterpret_cast<std::uint32_t *>(varMaterialTechniquePtr));
    }
}

void Load_MaterialTechniquePtrArray(bool atStreamStart, std::int32_t count)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialTechniquePtr), 4 * count);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    MaterialTechnique **entry = varMaterialTechniquePtr;
    for (std::int32_t index = 0; index < count; ++index, ++entry)
    {
        varMaterialTechniquePtr = entry;
        Load_MaterialTechniquePtr(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_MaterialTechniqueSet(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialTechniqueSet), 148);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &varMaterialTechniqueSet->name;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varMaterialTechniquePtr = varMaterialTechniqueSet->techniques;
        Load_MaterialTechniquePtrArray(false, 34);
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_MaterialTechniqueSetPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varMaterialTechniqueSetPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varMaterialTechniqueSetPtr)
    {
        const std::uintptr_t value =
            reinterpret_cast<std::uintptr_t>(*varMaterialTechniqueSetPtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varMaterialTechniqueSetPtr =
                reinterpret_cast<MaterialTechniqueSet *>(
                    AllocLoad_FxElemVisStateSample());
            varMaterialTechniqueSet = *varMaterialTechniqueSetPtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_MaterialTechniqueSet(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_MaterialTechniqueSetAsset(
                    reinterpret_cast<XAssetHeader *>(
                        varMaterialTechniqueSetPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varMaterialTechniqueSetPtr)->name);
                if (inserted) *inserted = *varMaterialTechniqueSetPtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varMaterialTechniqueSetPtr));
        }
    }
    DB_PopStreamPos();
}

