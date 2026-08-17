#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <gfx_d3d/r_font.h>

#include <cstdint>
#include <limits>

Font_s *varFont = nullptr;
Font_s **varFontHandle = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical Font loader requires the IW3 32-bit ABI");
static_assert(sizeof(Font_s) == 24u);
static_assert(sizeof(Glyph) == 24u);

void Load_Font(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varFont),
        sizeof(Font_s));
    if (DB_RuntimeGeneratedLoadFailed()) return;

    DB_PushStreamPos(4);
    varXString = &varFont->fontName;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varMaterialHandle = &varFont->material;
        Load_MaterialHandle(false);
    }
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varMaterialHandle = &varFont->glowMaterial;
        Load_MaterialHandle(false);
    }
    if (!DB_RuntimeGeneratedLoadFailed() && varFont->glyphs)
    {
        if (reinterpret_cast<std::uintptr_t>(varFont->glyphs) == UINT32_MAX)
        {
            if (varFont->glyphCount < 0 ||
                static_cast<std::uint64_t>(varFont->glyphCount) *
                    sizeof(Glyph) >
                    (std::numeric_limits<std::uint32_t>::max)())
            {
                DB_RuntimeGeneratedFailure("Font/glyph array");
            }
            else
            {
                const std::size_t bytes = static_cast<std::size_t>(
                    varFont->glyphCount) * sizeof(Glyph);
                if (!DB_RuntimeStreamCanRead(bytes))
                    DB_RuntimeGeneratedFailure("Font/glyph array");
                else
                {
                    varFont->glyphs = reinterpret_cast<Glyph *>(
                        AllocLoad_FxElemVisStateSample());
                    Load_Stream(true,
                        reinterpret_cast<std::uint8_t *>(varFont->glyphs),
                        static_cast<std::int32_t>(bytes));
                }
            }
        }
        else
        {
            DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(
                &varFont->glyphs));
        }
    }
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_FontHandle(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varFontHandle),
        4);
    if (DB_RuntimeGeneratedLoadFailed()) return;

    DB_PushStreamPos(0);
    if (*varFontHandle)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varFontHandle);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varFontHandle = reinterpret_cast<Font_s *>(
                AllocLoad_FxElemVisStateSample());
            varFont = *varFontHandle;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_Font(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_FontAsset(reinterpret_cast<XAssetHeader *>(varFontHandle));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varFontHandle)->fontName);
                if (inserted) *inserted = *varFontHandle;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varFontHandle));
        }
    }
    DB_PopStreamPos();
}
