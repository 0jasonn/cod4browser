#pragma once

#include <web/web_renderer.h>

#include <cstdint>
#include <vector>

struct DObj_s;
struct cpose_t;

struct WebRendererDObjSubmission
{
    const DObj_s *obj;
    const cpose_t *pose;
    std::uint32_t entityNumber;
    std::uint32_t renderFlags;
};

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

WebRendererDObjSceneResult WebRenderer_BuildDObjSceneCommand(
    const WebRendererDObjSubmission *submissions,
    std::uint32_t submissionCount,
    WebRendererDObjSceneCommand &destination);

const char *WebRenderer_DObjSceneResultString(
    WebRendererDObjSceneResult result) noexcept;
