#include "web/web_reverb.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

int main(int argc, char **argv)
{
    assert(!WebReverb_Input() && !WebReverb_Output());
    assert(!WebReverb_Process(128) && !WebReverb_SetRoom(0));
    assert(!WebReverb_Initialize(0) && !WebReverb_Initialize(192001));
    FILE *trace = argc > 1 ? std::fopen(argv[1], "wb") : nullptr;
    assert(argc <= 1 || trace);
    for (unsigned rate : {8000u, 44100u, 48000u, 96000u, 192000u})
        for (int room = 0; room < 26; ++room)
        {
            assert(WebReverb_Initialize(rate));
            assert(WebReverb_SetRoom(room));
            assert(!WebReverb_SetRoom(-1) && !WebReverb_SetRoom(26));
            assert(!WebReverb_Process(0) && !WebReverb_Process(1025));
            float *input = WebReverb_Input(), *output = WebReverb_Output();
            std::array<double, 4> energy{};
            float peak = 0;
            // Let the native effect's parameter crossfade settle, then excite
            // all first-order channels with a front-left impulse. No assets.
            const unsigned blocks = (2 * rate + 127) / 128;
            for (unsigned block = 0; block < blocks; ++block)
            {
                std::fill_n(input, 4096, 0.0f);
                if (block == 4) {
                    input[0] = 1.0f;
                    input[1024] = 1.0f;
                    input[3072] = std::sqrt(2.0f);
                }
                assert(WebReverb_Process(128));
                for (unsigned c = 0; c < 2; ++c)
                    for (unsigned i = 0; i < 128; ++i) {
                        const float sample = output[c * 1024 + i];
                        assert(std::isfinite(sample));
                        energy[std::min(3u, block * 4 / blocks)] += double(sample) * sample;
                        peak = std::max(peak, std::abs(sample));
                    }
                if (trace) {
                    assert(std::fwrite(output, sizeof(float), 128, trace) == 128);
                    assert(std::fwrite(output + 1024, sizeof(float), 128, trace) == 128);
                }
            }
            assert(peak > 0.00001f && peak < 2.0f);
            std::printf("rate=%u room=%d peak=%.9g energy=%.12g,%.12g,%.12g,%.12g\n",
                rate, room, peak, energy[0], energy[1], energy[2], energy[3]);
            // Refuse NaN before it can poison the feedback network.
            std::fill_n(input, 4096, 0.0f);
            input[0] = std::numeric_limits<float>::quiet_NaN();
            assert(!WebReverb_Process(128));
            for (unsigned c = 0; c < 2; ++c)
                for (unsigned i = 0; i < 128; ++i) assert(output[c * 1024 + i] == 0);
            input[0] = 0;
            assert(WebReverb_Process(128));
            // Exercise room changes with a live tail and odd block sizes.
            assert(WebReverb_SetRoom((room + 1) % 26));
            for (unsigned frames : {1u, 17u, 127u, 1024u}) assert(WebReverb_Process(frames));
            assert(input == WebReverb_Input() && output == WebReverb_Output());
            WebReverb_Shutdown();
            assert(!WebReverb_Input() && !WebReverb_Output());
        }
    if (trace) assert(std::fclose(trace) == 0);
}
