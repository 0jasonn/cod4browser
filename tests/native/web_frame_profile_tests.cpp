#include <web/web_frame_profile.h>
#include <web/web_frame_timing.h>

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
    // Ideal display streams with integer platform milliseconds must conserve
    // game time while retaining fractional 60 Hz admission phase.
    for (const unsigned hz : {60u, 120u, 144u, 165u, 240u})
    {
        WebFrameTiming timing;
        assert(timing.Advance(0u, 60) == 16);
        int admitted = 0;
        unsigned simulated = 0u;
        for (unsigned callback = 1u; callback <= hz * 10u; ++callback)
        {
            const auto now = callback * 1000u / hz;
            const int step = timing.Advance(now, 60);
            admitted += step > 0;
            simulated += step;
            assert(timing.Advance(now, 60) == 0); // duplicate callbacks
        }
        assert(admitted == 600);
        assert(simulated + timing.simulationElapsed == 10000u);
    }
    {
        WebFrameTiming timing;
        timing.Advance(10u, 60);
        assert(timing.Advance(17u, 60) == 0);
        assert(timing.Advance(17u, 125) == 0); // cap change cannot invent time
        assert(timing.Advance(18u, 125) == 8);
        assert(timing.Advance(19u, 30) == 0);
        assert(timing.Advance(52u, 30) == 34);
        assert(timing.Advance(2052u, 60) == 2000); // real gameplay stall retained
        assert(timing.Advance(12052u, 60) == 5000); // existing suspension limit
        assert(timing.Advance(12052u, 60) == 0);
        timing = {};
        timing.Advance(0u, 0);
        for (unsigned now = 1u; now <= 1000u; ++now)
            assert(timing.Advance(now, 0) == (now % 8u == 0 ? 8 : 0));
        timing = {};
        timing.Advance(UINT32_MAX - 7u, 60);
        assert(timing.Advance(12u, 60) == 20); // monotonic uint32 wrap
    }

    {
        WebFrameTiming timing;
        timing.Advance(100u, 125);
        assert(timing.Advance(105u, 125) == 0);
        // Loading ends after six seconds, with five milliseconds of admission
        // debt before it. Neither belongs to the new map's simulation.
        assert(timing.Advance(6100u, 125, 6100u) == 0);
        assert(timing.Advance(6107u, 125, 6100u) == 0);
        assert(timing.Advance(6108u, 125, 6100u) == 8);
        assert(timing.Advance(8108u, 125, 6100u) == 2000);
        // A restart resets the same clock; keep time spent after its reset.
        assert(timing.Advance(12020u, 125, 12000u) == 20);
        assert(timing.Advance(12020u, 125, 12000u) == 0);
        assert(timing.Advance(UINT32_MAX - 7u, 125, UINT32_MAX - 7u) == 0);
        assert(timing.Advance(12u, 125, UINT32_MAX - 7u) == 20);
    }

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
