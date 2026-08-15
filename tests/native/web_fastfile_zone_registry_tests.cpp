#include <web/web_fastfile_zone_registry.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
using kisak::fastfile::ZoneAssetRegistry;
using kisak::fastfile::ZoneRegistryError;
using kisak::fastfile::ZoneSpan;

constexpr std::uint32_t MATERIAL_TYPE = 0x04u;
constexpr std::uint32_t WORLD_TYPE = 0x10u;

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void RequireError(
    ZoneRegistryError actual,
    ZoneRegistryError expected,
    std::string_view message)
{
    if (actual != expected)
    {
        std::cerr << "FAIL: " << message << ": expected "
                  << kisak::fastfile::ZoneRegistryErrorString(expected)
                  << ", got "
                  << kisak::fastfile::ZoneRegistryErrorString(actual) << '\n';
        std::exit(1);
    }
}

std::array<std::uint32_t, kisak::fastfile::ZONE_STREAM_BLOCK_COUNT>
Declared()
{
    std::array<std::uint32_t, kisak::fastfile::ZONE_STREAM_BLOCK_COUNT> sizes{};
    sizes[0] = 732u;
    sizes[4] = 380u;
    return sizes;
}

void TestStableAssetsAndAliasLifecycle()
{
    ZoneAssetRegistry registry;
    RequireError(registry.Initialize(Declared()), ZoneRegistryError::None,
        "zone registry initializes");
    const ZoneSpan alias{4u, 16u, 4u};
    RequireError(registry.ReserveAlias(alias, MATERIAL_TYPE),
        ZoneRegistryError::None,
        "checked material alias cell reserves");
    std::uint32_t token = 0u;
    Require(kisak::fastfile::EncodeZoneAliasToken(alias, token) &&
            token == 0x40000011u,
        "alias token preserves exact one-based block encoding");

    std::uint32_t untouched = 77u;
    RequireError(registry.ResolveAlias(token, MATERIAL_TYPE, untouched),
        ZoneRegistryError::AliasUndefined,
        "reserved alias cannot resolve before publication");
    Require(untouched == 77u,
        "failed alias resolution leaves output unchanged");

    std::uint32_t materialIdentity = 0u;
    RequireError(registry.RegisterAsset(
            MATERIAL_TYPE, 0u, "web/synthetic", materialIdentity),
        ZoneRegistryError::None,
        "material registers into stable job-local storage");
    Require(materialIdentity == 1u,
        "first job-local registration receives identity one");
    RequireError(registry.PublishAlias(alias, materialIdentity),
        ZoneRegistryError::None,
        "registered material publishes through its reserved alias");
    std::uint32_t resolved = 0u;
    RequireError(registry.ResolveAlias(token, MATERIAL_TYPE, resolved),
        ZoneRegistryError::None,
        "defined material alias resolves");
    Require(resolved == materialIdentity && registry.DefinedAliasCount() == 1u,
        "alias resolves the same stable identity");

    std::uint32_t worldIdentity = 0u;
    RequireError(registry.RegisterAsset(WORLD_TYPE, 1u, {}, worldIdentity),
        ZoneRegistryError::None,
        "second top-level asset registers independently");
    Require(worldIdentity == 2u && registry.AssetCount() == 2u &&
            registry.TotalNameBytes() == 13u,
        "multi-asset identities and owned-name accounting are deterministic");
    const auto *material = registry.FindAsset(materialIdentity);
    Require(material && material->name == "web/synthetic" &&
            material->type == MATERIAL_TYPE && material->sourceIndex == 0u,
        "registry retains stable material metadata after later registration");

    RequireError(registry.PublishAlias(alias, materialIdentity),
        ZoneRegistryError::AliasDuplicate,
        "defined alias cannot be published twice");
    RequireError(registry.ResolveAlias(token, WORLD_TYPE, untouched),
        ZoneRegistryError::AssetTypeMismatch,
        "alias resolution enforces expected asset type");
}

void TestValidationLimitsAndAtomicity()
{
    ZoneAssetRegistry registry;
    RequireError(registry.Initialize(Declared(), {2u, 1u, 13u}),
        ZoneRegistryError::None,
        "exact registry limits initialize");
    const ZoneSpan alias{4u, 16u, 4u};
    RequireError(registry.ReserveAlias(alias, MATERIAL_TYPE),
        ZoneRegistryError::None,
        "exact alias limit accepts one cell");
    RequireError(registry.ReserveAlias({4u, 20u, 4u}, MATERIAL_TYPE),
        ZoneRegistryError::AliasLimit,
        "alias limit rejects a second cell");
    Require(registry.AliasCount() == 1u,
        "alias-limit failure leaves the registry unchanged");

    std::uint32_t materialIdentity = 99u;
    RequireError(registry.RegisterAsset(
            MATERIAL_TYPE, 0u, "web/synthetic", materialIdentity),
        ZoneRegistryError::None,
        "exact name-byte limit succeeds");
    Require(materialIdentity == 1u,
        "successful exact-limit registration updates output");
    std::uint32_t unchanged = 55u;
    RequireError(registry.RegisterAsset(WORLD_TYPE, 1u, "x", unchanged),
        ZoneRegistryError::NameBytesLimit,
        "cumulative registry name limit rejects one extra byte");
    Require(unchanged == 55u && registry.AssetCount() == 1u,
        "failed registration is output- and state-atomic");
    RequireError(registry.RegisterAsset(
            MATERIAL_TYPE, 0u, {}, unchanged),
        ZoneRegistryError::AssetDuplicate,
        "type/source pair cannot be registered twice");

    for (ZoneSpan invalid : {
            ZoneSpan{4u, 17u, 4u},
            ZoneSpan{4u, 380u, 4u},
            ZoneSpan{9u, 0u, 4u},
            ZoneSpan{4u, 16u, 8u}})
    {
        ZoneAssetRegistry candidate;
        RequireError(candidate.Initialize(Declared()), ZoneRegistryError::None,
            "invalid-alias fixture initializes");
        RequireError(candidate.ReserveAlias(invalid, MATERIAL_TYPE),
            ZoneRegistryError::AliasInvalid,
            "alias slot validates block, range, alignment, and width");
    }

    for (std::uint32_t invalidToken : {
            0u, 0xffffffffu, 0xfffffffeu, 0x30000011u,
            0x40000010u, 0x4000017du, 0x90000011u})
    {
        RequireError(registry.ResolveAlias(
                invalidToken, MATERIAL_TYPE, unchanged),
            ZoneRegistryError::AliasInvalid,
            "alias lookup rejects invalid token classes and spans");
    }
}

void TestUnloadResetAndStrings()
{
    ZoneAssetRegistry registry;
    RequireError(registry.Initialize(Declared()), ZoneRegistryError::None,
        "unload fixture initializes");
    const ZoneSpan alias{4u, 16u, 4u};
    RequireError(registry.ReserveAlias(alias, MATERIAL_TYPE),
        ZoneRegistryError::None, "unload alias reserves");
    std::uint32_t identity = 0u;
    RequireError(registry.RegisterAsset(
            MATERIAL_TYPE, 0u, "web/synthetic", identity),
        ZoneRegistryError::None, "unload asset registers");
    RequireError(registry.PublishAlias(alias, identity),
        ZoneRegistryError::None, "unload alias publishes");

    registry.UnloadAll();
    Require(registry.Initialized() && registry.AssetCount() == 0u &&
            registry.AliasCount() == 0u && registry.TotalNameBytes() == 0u,
        "UnloadAll releases identities, aliases, and owned names");
    RequireError(registry.RegisterAsset(
            MATERIAL_TYPE, 0u, "web/synthetic", identity),
        ZoneRegistryError::None,
        "registry remains configured after unload");
    Require(identity == 1u,
        "full unload restarts deterministic job-local identities");

    registry.Reset();
    Require(!registry.Initialized() && registry.AssetCount() == 0u,
        "Reset returns registry to uninitialized state");
    RequireError(registry.RegisterAsset(
            MATERIAL_TYPE, 0u, {}, identity),
        ZoneRegistryError::NotInitialized,
        "reset registry rejects later registration");

    for (ZoneRegistryError error : {
            ZoneRegistryError::None,
            ZoneRegistryError::NotInitialized,
            ZoneRegistryError::InvalidArgument,
            ZoneRegistryError::AssetLimit,
            ZoneRegistryError::AliasLimit,
            ZoneRegistryError::NameBytesLimit,
            ZoneRegistryError::AssetDuplicate,
            ZoneRegistryError::AssetUndefined,
            ZoneRegistryError::AssetTypeMismatch,
            ZoneRegistryError::AliasInvalid,
            ZoneRegistryError::AliasUndefined,
            ZoneRegistryError::AliasDuplicate,
            ZoneRegistryError::AllocationFailed})
    {
        Require(std::string_view(
                kisak::fastfile::ZoneRegistryErrorString(error)) !=
                "unknown zone registry error",
            "every zone-registry error has stable text");
    }
}
} // namespace

int main()
{
    TestStableAssetsAndAliasLifecycle();
    TestValidationLimitsAndAtomicity();
    TestUnloadResetAndStrings();
    std::cout << "web_fastfile_zone_registry_tests: PASS\n";
    return 0;
}
