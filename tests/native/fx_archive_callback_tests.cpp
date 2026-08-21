#include <EffectsCore/fx_archive_callbacks.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
struct Capture
{
    const FxEffectDef *effect = nullptr;
    void *data = nullptr;
    std::vector<std::uint8_t> bytes;
};

void __cdecl CaptureEntry(const FxEffectDef *effect, void *data)
{
    auto *capture = static_cast<Capture *>(data);
    capture->effect = effect;
    capture->data = data;
    const std::string name = effect->name ? effect->name : "";
    capture->bytes.insert(capture->bytes.end(), name.begin(), name.end());
    capture->bytes.push_back(0);
    const std::uint32_t key = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(effect));
    const auto *keyBytes = reinterpret_cast<const std::uint8_t *>(&key);
    capture->bytes.insert(capture->bytes.end(), keyBytes,
        keyBytes + sizeof(key));
}
}

int main()
{
    FxEffectDef effect{};
    effect.name = "fx/archive_callback";
    Capture capture{};
    XAssetHeader header{};
    header.fx = &effect;

    FX_ArchiveDispatchEffectDefAsset(header, &capture, CaptureEntry);

    assert(capture.effect == &effect);
    assert(capture.data == &capture);
    const std::string expectedName = "fx/archive_callback";
    assert(capture.bytes.size() == expectedName.size() + 1u + 4u);
    assert(std::memcmp(capture.bytes.data(), expectedName.data(),
        expectedName.size()) == 0);
    assert(capture.bytes[expectedName.size()] == 0);
    std::uint32_t key = 0;
    std::memcpy(&key, capture.bytes.data() + expectedName.size() + 1u,
        sizeof(key));
    assert(key == static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&effect)));
    return 0;
}
