// Compare the same synthetic impulse stream through native and Wasm OpenAL
// DSP. See tests/native/web_reverb_tests.cpp for the trace order and stimulus.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

assert.equal(process.argv.length, 4, "Usage: node tools/compare_reverb.mjs native.f32 wasm.f32");
const traces = process.argv.slice(2).map((path) => {
    const bytes = readFileSync(path);
    assert.equal(bytes.byteLength % 4, 0);
    return new Float32Array(bytes.buffer, bytes.byteOffset, bytes.byteLength / 4);
});
assert.equal(traces[0].length, traces[1].length);
let offset = 0, cases = 0, maxAbsoluteError = 0, maxRelativeRmsError = 0;
for (const rate of [8000, 44100, 48000, 96000, 192000]) {
    const samples = Math.ceil(2 * rate / 128) * 128 * 2;
    for (let room = 0; room < 26; ++room) {
        let errorEnergy = 0, referenceEnergy = 0, peakError = 0;
        assert.ok(offset + samples <= traces[0].length, "Truncated impulse trace");
        for (let i = offset; i < offset + samples; ++i) {
            const native = traces[0][i], wasm = traces[1][i];
            assert.ok(Number.isFinite(native) && Number.isFinite(wasm));
            const difference = wasm - native;
            peakError = Math.max(peakError, Math.abs(difference));
            errorEnergy += difference * difference;
            referenceEnergy += native * native;
        }
        assert.ok(referenceEnergy > 0);
        const relativeRmsError = Math.sqrt(errorEnergy / referenceEnergy);
        // Single-precision compiler/libm variation only: require each room and
        // rate to agree, so a quiet preset cannot hide behind a louder one.
        assert.ok(peakError <= 2e-6, `rate=${rate} room=${room} peak error=${peakError}`);
        assert.ok(relativeRmsError <= 1e-4,
            `rate=${rate} room=${room} relative RMS error=${relativeRmsError}`);
        maxAbsoluteError = Math.max(maxAbsoluteError, peakError);
        maxRelativeRmsError = Math.max(maxRelativeRmsError, relativeRmsError);
        offset += samples;
        ++cases;
    }
}
assert.equal(offset, traces[0].length, "Unexpected trailing impulse samples");
console.log(JSON.stringify({ cases, samples: offset, maxAbsoluteError, maxRelativeRmsError }));
