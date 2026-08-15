#include <web/web_engine_xmodel_material.h>

#include <cstddef>
#include <string_view>
#include <utility>

namespace
{
constexpr std::uint32_t GFX_MAPTYPE_2D = 3u;
constexpr std::uint32_t DXT1 = 0x31545844u;
constexpr std::uint32_t DXT3 = 0x33545844u;
constexpr std::uint32_t DXT5 = 0x35545844u;

bool SupportedIwiFormat(std::uint32_t format) noexcept
{
    return format == DXT1 || format == DXT3 || format == DXT5;
}

bool ValidExternalImageName(std::string_view name) noexcept
{
    if (name.empty() || name.front() == '/' || name.front() == '\\' ||
        name.starts_with(",$"))
    {
        return false;
    }

    bool segmentStart = true;
    std::uint32_t dotCount = 0u;
    for (const unsigned char byte : name)
    {
        if (byte < 0x21u || byte > 0x7eu || byte == '\\' || byte == ':')
        {
            return false;
        }
        if (byte == '/')
        {
            if (segmentStart || dotCount != 0u) return false;
            segmentStart = true;
            continue;
        }
        if (segmentStart)
        {
            dotCount = byte == '.' ? 1u : 0u;
            segmentStart = false;
        }
        else if (dotCount != 0u)
        {
            if (byte != '.' || dotCount == 2u) dotCount = 0u;
            else ++dotCount;
        }
    }
    return !segmentStart && dotCount == 0u;
}
} // namespace

WebEngineXModelMaterialResult WebEngine_SelectXModelColorMap(
    const kisak::fastfile::RetailWorldXModel &model,
    std::uint32_t surfaceMaterialIdentity,
    WebEngineXModelMaterialImageBinding &destination)
{
    using namespace kisak::fastfile;
    if (!model.published || !model.materialsTraversed)
        return WebEngineXModelMaterialResult::InvalidModel;
    if (surfaceMaterialIdentity == 0u)
        return WebEngineXModelMaterialResult::InvalidSurfaceMaterial;

    const RetailXModelMaterial *selectedMaterial = nullptr;
    const auto &resolvedMaterials = model.resolvedMaterials.empty()
        ? model.materials : model.resolvedMaterials;
    for (const RetailXModelMaterial &material : resolvedMaterials)
    {
        if (material.identity != surfaceMaterialIdentity) continue;
        if (selectedMaterial != nullptr)
            return WebEngineXModelMaterialResult::MaterialAmbiguous;
        selectedMaterial = &material;
    }
    if (!selectedMaterial || !selectedMaterial->published)
        return WebEngineXModelMaterialResult::MaterialNotFound;

    const RetailXModelMaterialTexture *selectedTexture = nullptr;
    for (const RetailXModelMaterialTexture &texture : selectedMaterial->textures)
    {
        if (texture.semantic != WEB_ENGINE_TEXTURE_SEMANTIC_COLOR_MAP) continue;
        if (selectedTexture != nullptr)
            return WebEngineXModelMaterialResult::ColorMapAmbiguous;
        selectedTexture = &texture;
    }
    if (!selectedTexture)
        return WebEngineXModelMaterialResult::ColorMapMissing;
    if (!selectedTexture->resolved || selectedTexture->imageIdentity == 0u)
        return WebEngineXModelMaterialResult::ImageUnresolved;

    const RetailXModelImage *selectedImage = nullptr;
    if (!model.resolvedImages.empty())
    {
        for (const RetailXModelImage &image : model.resolvedImages)
        {
            if (image.identity != selectedTexture->imageIdentity) continue;
            if (selectedImage != nullptr)
                return WebEngineXModelMaterialResult::ImageAmbiguous;
            selectedImage = &image;
        }
    }
    else
    {
        // Preserve compatibility with manually constructed host-native test
        // models that predate the parser's resolved-image catalog.
        for (const RetailXModelMaterial &material : model.materials)
        {
            for (const RetailXModelImage &image : material.images)
            {
                if (image.identity != selectedTexture->imageIdentity) continue;
                if (selectedImage != nullptr)
                    return WebEngineXModelMaterialResult::ImageAmbiguous;
                selectedImage = &image;
            }
        }
    }
    if (!selectedImage || !selectedImage->published)
        return WebEngineXModelMaterialResult::ImageNotFound;
    if (selectedImage->mapType == 0u || selectedImage->name.starts_with(",$"))
        return WebEngineXModelMaterialResult::BuiltinUnsupported;
    if (selectedImage->mapType != GFX_MAPTYPE_2D ||
        !selectedImage->loadDefTraversed || selectedImage->width == 0u ||
        selectedImage->height == 0u || selectedImage->depth != 1u ||
        !SupportedIwiFormat(selectedImage->format) ||
        selectedImage->resourceBytes != 0u)
    {
        return WebEngineXModelMaterialResult::ImageLayoutUnsupported;
    }
    if (!ValidExternalImageName(selectedImage->name))
        return WebEngineXModelMaterialResult::ImageNameInvalid;

    try
    {
        WebEngineXModelMaterialImageBinding replacement;
        replacement.materialName = selectedMaterial->name;
        replacement.imageName = selectedImage->name;
        replacement.imagePath = "images/";
        replacement.imagePath += selectedImage->name;
        replacement.imagePath += ".iwi";
        replacement.materialIdentity = selectedMaterial->identity;
        replacement.imageIdentity = selectedImage->identity;
        replacement.samplerState = selectedTexture->samplerState;
        replacement.semantic = selectedTexture->semantic;
        destination = std::move(replacement);
    }
    catch (...)
    {
        return WebEngineXModelMaterialResult::AllocationFailed;
    }
    return WebEngineXModelMaterialResult::Success;
}

const char *WebEngine_XModelMaterialResultString(
    WebEngineXModelMaterialResult result) noexcept
{
    switch (result)
    {
    case WebEngineXModelMaterialResult::Success:
        return "success";
    case WebEngineXModelMaterialResult::InvalidModel:
        return "the XModel dependency chain is not published";
    case WebEngineXModelMaterialResult::InvalidSurfaceMaterial:
        return "the selected XSurface has no material identity";
    case WebEngineXModelMaterialResult::MaterialNotFound:
        return "the selected XSurface material is not published";
    case WebEngineXModelMaterialResult::MaterialAmbiguous:
        return "the selected XSurface material identity is ambiguous";
    case WebEngineXModelMaterialResult::ColorMapMissing:
        return "the selected material has no color-map texture";
    case WebEngineXModelMaterialResult::ColorMapAmbiguous:
        return "the selected material has multiple color-map textures";
    case WebEngineXModelMaterialResult::ImageUnresolved:
        return "the selected color-map image identity is unresolved";
    case WebEngineXModelMaterialResult::ImageNotFound:
        return "the selected color-map image is not published";
    case WebEngineXModelMaterialResult::ImageAmbiguous:
        return "the selected color-map image identity is ambiguous";
    case WebEngineXModelMaterialResult::BuiltinUnsupported:
        return "the selected color map is a built-in image";
    case WebEngineXModelMaterialResult::ImageLayoutUnsupported:
        return "the selected color map is not an external bounded 2D DXT image";
    case WebEngineXModelMaterialResult::ImageNameInvalid:
        return "the selected color-map image name is unsafe";
    case WebEngineXModelMaterialResult::AllocationFailed:
        return "the color-map binding could not be allocated";
    }
    return "unknown XModel material selection error";
}
