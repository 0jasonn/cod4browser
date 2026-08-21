#include <web/web_renderer_dobj_scene.h>
#include <universal/q_shared.h>
#include <xanim/xmodel.h>

#include <cassert>
#include <cstdint>

namespace
{
float g_lodDistance = -1.0f;
int g_canonicalLod = 2;

void TestLodDelegatesToCanonicalXModelPolicy()
{
    XModel model{};
    model.numLods = 3;
    const float poseOrigin[3] = {3.0f, 4.0f, 0.0f};
    const float viewOrigin[3] = {0.0f, 0.0f, 0.0f};

    g_lodDistance = -1.0f;
    g_canonicalLod = 2;
    assert(WebRenderer_SelectDObjLod(
        &model, poseOrigin, viewOrigin) == 2);
    assert(g_lodDistance == 5.0f);

    g_canonicalLod = -1;
    assert(WebRenderer_SelectDObjLod(
        &model, poseOrigin, viewOrigin) == 2);
    assert(WebRenderer_SelectDObjLod(&model, poseOrigin, nullptr) == 0);
    assert(WebRenderer_SelectDObjLod(nullptr, poseOrigin, viewOrigin) == 0);
}

void TestOrdinaryAndViewmodelFlagsShareAdmission()
{
    const DObj_s *object = reinterpret_cast<const DObj_s *>(0x1u);
    const cpose_t *pose = reinterpret_cast<const cpose_t *>(0x1u);

    for (const std::uint32_t renderFlags : {0u, 3u, 4u, 7u})
    {
        const WebRendererDObjSubmission submission{
            object, pose, 17u, renderFlags};
        assert(WebRenderer_ValidateDObjSubmission(submission, 0u) ==
            WebRendererDObjAdmissionResult::Accepted);
    }
}

void TestInvalidAndCapacityAdmissionIsDeterministic()
{
    const DObj_s *object = reinterpret_cast<const DObj_s *>(0x1u);
    const cpose_t *pose = reinterpret_cast<const cpose_t *>(0x1u);
    const WebRendererDObjSubmission valid{object, pose, 1u, 0u};
    const WebRendererDObjSubmission invalid{nullptr, pose, 1u, 0u};

    assert(WebRenderer_ValidateDObjSubmission(invalid, 0u) ==
        WebRendererDObjAdmissionResult::InvalidSubmission);
    assert(WebRenderer_ValidateDObjSubmission(valid, 511u, 512u) ==
        WebRendererDObjAdmissionResult::Accepted);
    assert(WebRenderer_ValidateDObjSubmission(valid, 512u, 512u) ==
        WebRendererDObjAdmissionResult::LimitReached);
}
} // namespace

int main()
{
    TestLodDelegatesToCanonicalXModelPolicy();
    TestOrdinaryAndViewmodelFlagsShareAdmission();
    TestInvalidAndCapacityAdmissionIsDeterministic();
    return 0;
}

int __cdecl XModelGetLodForDist(const XModel *, float distance)
{
    g_lodDistance = distance;
    return g_canonicalLod;
}
