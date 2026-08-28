#include <web/web_frame_profile.h>

#include <cassert>

namespace
{
constexpr std::uint32_t CONTEXT = 7u;
constexpr std::uint32_t WORLD = 11u;

void Finish(WebFrameProfileCapture &capture, double now,
    bool gameplayFrame, bool rendererSubmitted,
    std::uint32_t context = CONTEXT, std::uint32_t world = WORLD)
{
    capture.FinishPump(
        now, gameplayFrame, rendererSubmitted, context, world);
}
}

int main()
{
    constexpr WebFrameProfileGpuStage stages[] = {
        WebFrameProfileGpuStage::World,
        WebFrameProfileGpuStage::StaticModels,
        WebFrameProfileGpuStage::SunShadows,
        WebFrameProfileGpuStage::SpotShadows,
        WebFrameProfileGpuStage::DynamicFx,
        WebFrameProfileGpuStage::UiPost,
    };
    for (std::uint32_t ordinal = 0u; ordinal < 12u; ++ordinal)
    {
        assert(WebFrameProfile_GpuStageForOrdinal(ordinal) ==
            stages[ordinal % 6u]);
        assert(WebFrameProfile_GpuStageName(stages[ordinal % 6u])[0] != '\0');
    }

    WebFrameProfileCapture capture;

    capture.Begin(5u, 0.0, 100.0);
    Finish(capture, 1.0, false, true);
    Finish(capture, 2.0, true, true);
    Finish(capture, 3.0, false, false);
    Finish(capture, 4.0, true, true);
    Finish(capture, 5.0, true, false);
    assert(capture.collectedSamples == 2u);
    assert(capture.Remaining() == 3u);

    capture.Begin(1u, 10.0, 100.0);
    Finish(capture, 11.0, false, true);
    assert(capture.collectedSamples == 0u);
    Finish(capture, 12.0, true, false);
    assert(capture.collectedSamples == 0u);
    assert(capture.FinishPump(13.0, true, true, CONTEXT, WORLD) ==
        WebFrameProfilePumpResult::CaptureComplete);
    assert(capture.state == WebFrameProfileCaptureState::Complete);
    assert(capture.Remaining() == 0u);

    capture.Begin(3u, 20.0, 5.0);
    Finish(capture, 21.0, true, true);
    assert(capture.Poll(25.0));
    assert(capture.state == WebFrameProfileCaptureState::Incomplete);
    assert(capture.incompleteReason ==
        WebFrameProfileIncompleteReason::Timeout);
    assert(capture.collectedSamples == 1u);

    capture.Begin(2u, 30.0, 100.0);
    assert(capture.collectedSamples == 0u);
    assert(capture.Remaining() == 2u);
    assert(capture.incompleteReason == WebFrameProfileIncompleteReason::None);

    Finish(capture, 31.0, true, false, CONTEXT + 1u, WORLD);
    assert(capture.collectedSamples == 0u);
    Finish(capture, 32.0, true, true);
    assert(capture.collectedSamples == 1u);
    assert(capture.FinishPump(33.0, true, true, CONTEXT + 1u, WORLD) ==
        WebFrameProfilePumpResult::CaptureIncomplete);
    assert(capture.collectedSamples == 1u);
    assert(capture.incompleteReason ==
        WebFrameProfileIncompleteReason::ContextChanged);

    capture.Begin(3u, 40.0, 100.0);
    Finish(capture, 41.0, true, true);
    assert(capture.FinishPump(42.0, true, true, CONTEXT, WORLD + 1u) ==
        WebFrameProfilePumpResult::CaptureIncomplete);
    assert(capture.collectedSamples == 1u);
    assert(capture.incompleteReason ==
        WebFrameProfileIncompleteReason::WorldChanged);
}
