#pragma once

#include <web/web_renderer.h>
#include <web/web_renderer_lighting.h>
#include <web/web_renderer_lod.h>

#include <cstdint>
#include <string_view>
#include <vector>

struct DObj_s;
struct cpose_t;
struct XModel;

using WebRendererMaterialResolver = Material *(*)(Material *) noexcept;

inline Material *WebRenderer_ResolveDObjMaterial(
    Material *material, WebRendererMaterialResolver resolver) noexcept
{
    if (!resolver) return material;
    Material *canonical = resolver(material);
    return canonical ? canonical : material;
}

inline bool WebRenderer_IsReflexSightTechnique(
    const char *techniqueName) noexcept
{
    return techniqueName &&
        std::string_view(techniqueName).starts_with("reflexsight");
}

struct WebRendererDObjSubmission
{
    const DObj_s *obj;
    const cpose_t *pose;
    std::uint32_t entityNumber;
    std::uint32_t renderFlags;
    float lightingOrigin[3];
    std::uint8_t reflectionProbeIndex = 0u;
    const GfxImage *reflectionProbeImage = nullptr;
};

constexpr bool WebRenderer_DObjUsesDepthHack(
    std::uint32_t renderFlags) noexcept
{
    return (renderFlags & 2u) != 0u;
}

constexpr bool WebRenderer_DObjIsSunShadowCandidate(
    std::uint32_t renderFlags) noexcept
{
    // Native sun-shadow DPVS uses renderFxFlagsCull=1.
    return (renderFlags & 1u) == 0u;
}

enum class WebRendererDObjAdmissionResult : std::uint8_t
{
    Accepted = 0,
    InvalidSubmission,
    LimitReached,
};

inline WebRendererDObjAdmissionResult WebRenderer_ValidateDObjSubmission(
    const WebRendererDObjSubmission &submission,
    std::uint32_t currentCount,
    std::uint32_t capacity = WEB_RENDERER_MAX_DYNAMIC_DOBJ_SUBMISSIONS) noexcept
{
    if (!submission.obj || !submission.pose)
        return WebRendererDObjAdmissionResult::InvalidSubmission;
    if (currentCount >= capacity)
        return WebRendererDObjAdmissionResult::LimitReached;
    return WebRendererDObjAdmissionResult::Accepted;
}

struct WebRendererDObjSceneCommand
{
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<WebRendererWorldBatchDesc> batches;
    WebRendererModelLightingAtlas modelLightingAtlas;
    std::uint32_t dobjCount = 0u;
    std::uint32_t modelCount = 0u;
    std::uint32_t surfaceCount = 0u;
};

enum class WebRendererDObjSceneResult : std::uint8_t
{
    Success = 0,
    NoDObj,
    InvalidSubmission,
    InvalidModel,
    IndexOutOfRange,
    OutputTooLarge,
    AllocationFailed,
};

// Keep distance validation at the portable renderer seam while delegating the
// actual threshold decision to canonical XModelGetLodForDist.
int WebRenderer_SelectDObjLod(
    const XModel *model, const float poseOrigin[3],
    const WebRendererLodParms *lodParms) noexcept;

WebRendererDObjSceneResult WebRenderer_BuildDObjSceneCommand(
    const WebRendererDObjSubmission *submissions,
    std::uint32_t submissionCount,
    WebRendererDObjSceneCommand &destination,
    const WebRendererLodParms *lodParms = nullptr,
    const GfxLightGrid *lightGrid = nullptr,
    const WebRendererModelLightingCallbacks *lightingCallbacks = nullptr,
    WebRendererMaterialResolver materialResolver = nullptr);

const char *WebRenderer_DObjSceneResultString(
    WebRendererDObjSceneResult result) noexcept;
