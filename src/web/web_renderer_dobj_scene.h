#pragma once

#include <web/web_renderer.h>

#include <cstdint>
#include <vector>

struct DObj_s;
struct cpose_t;
struct XModel;

struct WebRendererDObjSubmission
{
    const DObj_s *obj;
    const cpose_t *pose;
    std::uint32_t entityNumber;
    std::uint32_t renderFlags;
};

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
    const float viewOrigin[3]) noexcept;

WebRendererDObjSceneResult WebRenderer_BuildDObjSceneCommand(
    const WebRendererDObjSubmission *submissions,
    std::uint32_t submissionCount,
    WebRendererDObjSceneCommand &destination,
    const float *viewOrigin = nullptr);

const char *WebRenderer_DObjSceneResultString(
    WebRendererDObjSceneResult result) noexcept;
