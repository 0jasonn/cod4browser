#include "web_reverb.h"
#include "sound/snd_reverb_presets.h"

#include "alc/effects/base.h"
#include "core/bformatdec.h"
#include "core/context.h"
#include "core/device.h"
#include "core/effectslot.h"
#include "core/fpu_ctrl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace {
static_assert(BufferLineSize == 1024, "Review the browser planar-buffer ABI");
// Use OpenAL's DSP directly without opening a host device or starting its
// mixer/event threads. Web Audio owns scheduling and the output device.
struct Device final : DeviceBase {
    Device() : DeviceBase{DeviceType::Loopback} {}
};
struct Context final : ContextBase {
    explicit Context(DeviceBase *device) : ContextBase{gsl::not_null{device}} {}
};

// OpenAL Soft 1.25.2 alc/panning.cpp StereoConfig: first-order, 2D, N3D.
constexpr std::array<ChannelDec, 2> StereoCoefficients{{
    {0.5f, 0.288675135f, 0.0552305643f},
    {0.5f, -0.288675135f, 0.0552305643f}}};

struct Reverb {
    Device device;
    Context context{&device};
    EffectSlotBase slot;
    alignas(16) std::array<FloatBufferLine, 4> input{};
    alignas(16) std::array<FloatBufferLine, 3> wet{};
    alignas(16) std::array<FloatBufferLine, 2> output{};
    al::intrusive_ptr<EffectState> state{ReverbStateFactory_getFactory()->create()};
    BFormatDec decoder;

    explicit Reverb(unsigned rate) : decoder{3, StereoCoefficients, {}, 400.0f / rate}
    {
        device.mSampleRate = rate;
        device.mAmbiOrder = 1;
        device.m2DMixing = true;
        device.Dry.Buffer = wet;
        device.Dry.AmbiMap[0] = {1.0f, 0};
        device.Dry.AmbiMap[1] = {1.0f, 1};
        device.Dry.AmbiMap[2] = {1.0f, 3};
        state->deviceUpdate(&device, nullptr);
    }
};
std::unique_ptr<Reverb> reverb;
}

int WebReverb_Initialize(unsigned sampleRate)
{
    if (sampleRate < 8000 || sampleRate > 192000) return 0;
    reverb = std::make_unique<Reverb>(sampleRate);
    return WebReverb_SetRoom(0);
}

int WebReverb_SetRoom(int room)
{
    if (!reverb || room < 0 || room >= static_cast<int>(std::size(AL_RoomPresets))) return 0;
    const FPUCtl mixerMode;
    const auto &p = AL_RoomPresets[room];
    const EffectProps props{ReverbProps{
        p.flDensity, p.flDiffusion, p.flGain, p.flGainHF, p.flGainLF,
        p.flDecayTime, p.flDecayHFRatio, p.flDecayLFRatio,
        p.flReflectionsGain, p.flReflectionsDelay,
        {p.flReflectionsPan[0], p.flReflectionsPan[1], p.flReflectionsPan[2]},
        p.flLateReverbGain, p.flLateReverbDelay,
        {p.flLateReverbPan[0], p.flLateReverbPan[1], p.flLateReverbPan[2]},
        p.flEchoTime, p.flEchoDepth, p.flModulationTime, p.flModulationDepth,
        p.flAirAbsorptionGainHF, p.flHFReference, p.flLFReference,
        p.flRoomRolloffFactor, p.iDecayHFLimit != 0}};
    reverb->state->update(&reverb->context, &reverb->slot, &props,
        {&reverb->device.Dry, &reverb->device.RealOut});
    return 1;
}

float *WebReverb_Input() { return reverb ? reverb->input[0].data() : nullptr; }
float *WebReverb_Output() { return reverb ? reverb->output[0].data() : nullptr; }

int WebReverb_Process(unsigned frames)
{
    if (!reverb || frames == 0 || frames > BufferLineSize) return 0;
    for (auto &line : reverb->output) std::fill_n(line.begin(), frames, 0.0f);
    for (const auto &line : reverb->input)
        for (unsigned i = 0; i < frames; ++i)
            if (!std::isfinite(line[i])) return 0;
    for (auto &line : reverb->wet) std::fill_n(line.begin(), frames, 0.0f);
    const FPUCtl mixerMode;
    reverb->state->process(frames, reverb->input, reverb->wet);
    reverb->decoder.process(reverb->output, reverb->wet, frames);
    return 1;
}

void WebReverb_Shutdown() { reverb.reset(); }
