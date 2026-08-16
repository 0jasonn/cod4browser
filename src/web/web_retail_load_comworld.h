#pragma once

#include <web/web_retail_load_context.h>

#include <cstdint>
#include <memory>

namespace kisak::fastfile
{

enum class RetailComWorldLoadProgress : std::uint8_t
{
    Idle = 0,
    Running,
    Complete,
};

// Resumable transcription of native Load_ComWorldPtr, Load_ComWorld,
// Load_ComPrimaryLightArray, and Load_ComPrimaryLight.  The family owns no
// rendering behavior; top-level XAsset dispatch remains in the census job.
class RetailComWorldLoadFamily
{
public:
    struct State;

    RetailComWorldLoadFamily() noexcept;
    ~RetailComWorldLoadFamily();
    RetailComWorldLoadFamily(const RetailComWorldLoadFamily &) = delete;
    RetailComWorldLoadFamily &operator=(const RetailComWorldLoadFamily &) = delete;

    RetailCensusError Begin(
        RetailLoadContext &context,
        std::uint32_t assetIndex,
        std::uint32_t serializedReference) noexcept;
    RetailCensusError Step(RetailLoadContext &context) noexcept;
    RetailComWorldLoadProgress Progress() const noexcept;
    void Reset() noexcept;

private:
    std::unique_ptr<State> state_;
};

} // namespace kisak::fastfile
