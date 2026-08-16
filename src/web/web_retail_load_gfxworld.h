#pragma once

#include <web/web_retail_load_context.h>

#include <cstdint>
#include <memory>

namespace kisak::fastfile
{

enum class RetailGfxWorldLoadProgress : std::uint8_t
{
    Idle = 0,
    Running,
    Complete,
};

// Resumable native-order transcription of Load_GfxWorldPtr, Load_GfxWorld,
// and their complete generated child graph.  This family owns only canonical
// CPU database data.  Runtime texture and vertex-buffer fields remain neutral.
class RetailGfxWorldLoadFamily
{
public:
    struct State;

    RetailGfxWorldLoadFamily() noexcept;
    ~RetailGfxWorldLoadFamily();
    RetailGfxWorldLoadFamily(const RetailGfxWorldLoadFamily &) = delete;
    RetailGfxWorldLoadFamily &operator=(const RetailGfxWorldLoadFamily &) = delete;

    RetailCensusError Begin(
        RetailLoadContext &context,
        std::uint32_t assetIndex,
        std::uint32_t serializedReference) noexcept;
    RetailCensusError Step(RetailLoadContext &context) noexcept;
    RetailGfxWorldLoadProgress Progress() const noexcept;
    void Reset() noexcept;

private:
    std::unique_ptr<State> state_;
};

} // namespace kisak::fastfile
