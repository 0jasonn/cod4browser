#include <web/web_engine_xmodel_material.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

namespace
{
using namespace kisak::fastfile;

void Require(bool condition, const char *message)
{
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

RetailWorldXModel MakeModel()
{
    RetailWorldXModel model;
    model.published = true;
    model.materialsTraversed = true;

    RetailXModelMaterial imageOwner;
    imageOwner.name = "web/other_material";
    imageOwner.identity = 20u;
    imageOwner.published = true;
    RetailXModelImage image;
    image.textureIndex = 0u;
    image.name = "web/wall_color";
    image.mapType = 3u;
    image.width = 64u;
    image.height = 32u;
    image.depth = 1u;
    image.format = 0x31545844u;
    image.identity = 31u;
    image.loadDefTraversed = true;
    image.published = true;
    imageOwner.images.push_back(std::move(image));

    RetailXModelMaterial selected;
    selected.name = "web/wall";
    selected.identity = 10u;
    selected.published = true;
    RetailXModelMaterialTexture normal;
    normal.semantic = 5u;
    normal.imageIdentity = 32u;
    normal.resolved = true;
    selected.textures.push_back(normal);
    RetailXModelMaterialTexture color;
    color.semantic = WEB_ENGINE_TEXTURE_SEMANTIC_COLOR_MAP;
    color.samplerState = 0x62u;
    color.imageIdentity = 31u;
    color.resolved = true;
    selected.textures.push_back(color);

    model.materials.push_back(std::move(imageOwner));
    model.materials.push_back(std::move(selected));
    return model;
}

void TestTypedColorMapSelection()
{
    const RetailWorldXModel model = MakeModel();
    WebEngineXModelMaterialImageBinding binding;
    Require(WebEngine_SelectXModelColorMap(model, 10u, binding) ==
            WebEngineXModelMaterialResult::Success,
        "published surface material resolves its typed color-map image");
    Require(binding.materialName == "web/wall" &&
            binding.imageName == "web/wall_color" &&
            binding.imagePath == "images/web/wall_color.iwi" &&
            binding.materialIdentity == 10u && binding.imageIdentity == 31u &&
            binding.samplerState == 0x62u &&
            binding.semantic == WEB_ENGINE_TEXTURE_SEMANTIC_COLOR_MAP,
        "binding copies exact material and image metadata");
}

void TestFailClosedSelection()
{
    const WebEngineXModelMaterialImageBinding sentinel{
        "sentinel-material", "sentinel-image", "images/sentinel.iwi",
        7u, 8u, 9u, 10u};

    auto requireUnchanged = [&](RetailWorldXModel model,
                                WebEngineXModelMaterialResult expected,
                                const char *message) {
        WebEngineXModelMaterialImageBinding destination = sentinel;
        const auto actual = WebEngine_SelectXModelColorMap(model, 10u, destination);
        Require(actual == expected, message);
        Require(destination.materialName == sentinel.materialName &&
                destination.imageName == sentinel.imageName &&
                destination.imagePath == sentinel.imagePath &&
                destination.materialIdentity == sentinel.materialIdentity &&
                destination.imageIdentity == sentinel.imageIdentity &&
                destination.samplerState == sentinel.samplerState &&
                destination.semantic == sentinel.semantic,
            "failed selection is atomic");
    };

    RetailWorldXModel model = MakeModel();
    model.materials[1].textures[1].semantic = 5u;
    requireUnchanged(std::move(model), WebEngineXModelMaterialResult::ColorMapMissing,
        "filename order cannot substitute for color-map semantics");

    model = MakeModel();
    model.materials[1].textures.push_back(model.materials[1].textures[1]);
    requireUnchanged(std::move(model), WebEngineXModelMaterialResult::ColorMapAmbiguous,
        "multiple color-map records are rejected");

    model = MakeModel();
    model.materials[1].textures[1].resolved = false;
    requireUnchanged(std::move(model), WebEngineXModelMaterialResult::ImageUnresolved,
        "unresolved image identity is rejected");

    model = MakeModel();
    model.materials[0].images[0].name = ",$identitynormalmap";
    model.materials[0].images[0].mapType = 0u;
    model.materials[0].images[0].loadDefTraversed = false;
    requireUnchanged(std::move(model), WebEngineXModelMaterialResult::BuiltinUnsupported,
        "built-in images remain outside the IWD/IWI boundary");

    model = MakeModel();
    model.materials[0].images[0].resourceBytes = 8u;
    requireUnchanged(std::move(model),
        WebEngineXModelMaterialResult::ImageLayoutUnsupported,
        "embedded image resources cannot be relabeled as external IWI members");

    model = MakeModel();
    model.materials[0].images[0].name = "../escape";
    requireUnchanged(std::move(model), WebEngineXModelMaterialResult::ImageNameInvalid,
        "unsafe image names cannot form archive paths");
}

void TestResultStrings()
{
    Require(std::strcmp(WebEngine_XModelMaterialResultString(
            WebEngineXModelMaterialResult::Success), "success") == 0,
        "success result string is stable");
    Require(std::strcmp(WebEngine_XModelMaterialResultString(
            static_cast<WebEngineXModelMaterialResult>(0xffu)),
            "unknown XModel material selection error") == 0,
        "unknown result string is stable");
}
} // namespace

int main()
{
    TestTypedColorMapSelection();
    TestFailClosedSelection();
    TestResultStrings();
    std::cout << "web engine XModel material tests passed\n";
    return 0;
}
