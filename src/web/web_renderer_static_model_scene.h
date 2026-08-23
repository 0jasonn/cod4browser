#pragma once

#include <web/web_renderer.h>
#include <web/web_renderer_lighting.h>

#include <cstdint>
#include <vector>

struct GfxWorld;

using WebRendererStaticMaterialResolver = Material *(*)(Material *) noexcept;

struct WebRendererStaticModelSceneCommand
{
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<WebRendererStaticModelInstanceDesc> instances;
    std::vector<WebRendererStaticModelBatchDesc> batches;
    WebRendererModelLightingAtlas modelLightingAtlas;
    std::uint32_t modelCount = 0u;
    std::uint32_t surfaceCount = 0u;
    std::uint32_t canonicalInstanceCount = 0u;
    std::uint32_t modelLightingFailureCount = 0u;
    bool modelLightingSourceAvailable = false;
};

enum class WebRendererStaticModelSceneResult : std::uint8_t
{
    Success = 0,
    NoStaticModels,
    InvalidWorld,
    InvalidModel,
    InvalidPlacement,
    IndexOutOfRange,
    OutputTooLarge,
    AllocationFailed,
};

WebRendererStaticModelSceneResult WebRenderer_BuildStaticModelSceneCommand(
    const GfxWorld &world,
    WebRendererStaticModelSceneCommand &destination,
    const WebRendererModelLightingCallbacks *lightingCallbacks = nullptr,
    WebRendererStaticMaterialResolver materialResolver = nullptr);

const char *WebRenderer_StaticModelSceneResultString(
    WebRendererStaticModelSceneResult result) noexcept;
