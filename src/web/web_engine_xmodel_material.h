#pragma once

#include <web/web_retail_fastfile_census.h>

#include <cstdint>
#include <string>

constexpr std::uint8_t WEB_ENGINE_TEXTURE_SEMANTIC_COLOR_MAP = 2u;

struct WebEngineXModelMaterialImageBinding
{
    std::string materialName;
    std::string imageName;
    std::string imagePath;
    std::uint32_t materialIdentity = 0u;
    std::uint32_t imageIdentity = 0u;
    std::uint8_t samplerState = 0u;
    std::uint8_t semantic = 0u;
};

enum class WebEngineXModelMaterialResult : std::uint8_t
{
    Success = 0,
    InvalidModel,
    InvalidSurfaceMaterial,
    MaterialNotFound,
    MaterialAmbiguous,
    ColorMapMissing,
    ColorMapAmbiguous,
    ImageUnresolved,
    ImageNotFound,
    ImageAmbiguous,
    BuiltinUnsupported,
    ImageLayoutUnsupported,
    ImageNameInvalid,
    AllocationFailed,
};

// Resolves exactly one published TS_COLOR_MAP texture from the material used by
// the selected XSurface. Asset identities, rather than vector or filename
// order, join the material texture to its GfxImage. Destination is unchanged on
// every error and contains metadata only; no parser-owned bytes are retained.
WebEngineXModelMaterialResult WebEngine_SelectXModelColorMap(
    const kisak::fastfile::RetailWorldXModel &model,
    std::uint32_t surfaceMaterialIdentity,
    WebEngineXModelMaterialImageBinding &destination);

const char *WebEngine_XModelMaterialResultString(
    WebEngineXModelMaterialResult result) noexcept;
