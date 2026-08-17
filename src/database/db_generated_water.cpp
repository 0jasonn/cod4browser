#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_generated_water.h>
#include <database/db_runtime_prefix.h>

#include <cstddef>
#include <cstdint>
#include <limits>

water_t *varwater_t = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical water loader requires the IW3 32-bit ABI");
static_assert(sizeof(water_t) == 68u);
static_assert(sizeof(complex_s) == 8u);

bool CheckedGridBytes(std::int32_t m, std::int32_t n, std::size_t stride,
    const char *failureStage, std::size_t &bytes)
{
    if (m < 0 || n < 0)
    {
        DB_RuntimeGeneratedFailure(failureStage);
        return false;
    }
    const std::uint64_t count = static_cast<std::uint64_t>(m) *
        static_cast<std::uint64_t>(n);
    if (count > (std::numeric_limits<std::uint32_t>::max)() / stride)
    {
        DB_RuntimeGeneratedFailure(failureStage);
        return false;
    }
    bytes = static_cast<std::size_t>(count * stride);
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure(failureStage);
        return false;
    }
    return true;
}
} // namespace

void __cdecl Load_water_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varwater_t),
        sizeof(water_t));
    if (DB_RuntimeGeneratedLoadFailed()) return;

    if (varwater_t->H0)
    {
        std::size_t bytes = 0;
        if (!CheckedGridBytes(varwater_t->M, varwater_t->N,
            sizeof(complex_s), "water/H0 array", bytes)) return;
        varwater_t->H0 = reinterpret_cast<complex_s *>(
            AllocLoad_FxElemVisStateSample());
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(varwater_t->H0),
            static_cast<std::int32_t>(bytes));
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
    if (varwater_t->wTerm)
    {
        std::size_t bytes = 0;
        if (!CheckedGridBytes(varwater_t->M, varwater_t->N,
            sizeof(float), "water/wTerm array", bytes)) return;
        varwater_t->wTerm = reinterpret_cast<float *>(
            AllocLoad_FxElemVisStateSample());
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(varwater_t->wTerm),
            static_cast<std::int32_t>(bytes));
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
    varGfxImagePtr = &varwater_t->image;
    Load_GfxImagePtr(false);
}
