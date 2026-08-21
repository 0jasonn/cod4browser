#include <web/web_renderer_dobj_scene.h>

#include <cassert>
#include <cstdint>

namespace
{
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
    TestOrdinaryAndViewmodelFlagsShareAdmission();
    TestInvalidAndCapacityAdmissionIsDeterministic();
    return 0;
}
