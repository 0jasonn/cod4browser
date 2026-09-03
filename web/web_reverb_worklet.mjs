import createReverb from "./reverb_dsp.mjs";

const SOURCES = 54;
const STRIDE = 1024;
const SQRT3 = Math.sqrt(3);
const PARAMS = Array.from({ length: SOURCES }, (_, id) =>
    Array.from({ length: 4 }, (_, channel) => `s${id}_${channel}`));

// Only device PCM and gain history live here. Source selection, lifetime,
// alias rules, room identity and wet-level fades remain in Kisak's SND.
class KisakReverbProcessor extends AudioWorkletProcessor {
    static get parameterDescriptors() {
        return [{ name: "room", defaultValue: 0, minValue: 0, maxValue: 25, automationRate: "k-rate" },
            ...PARAMS.flat().map((name) => ({ name, defaultValue: 0,
                minValue: -32, maxValue: 32, automationRate: "k-rate" }))];
    }

    constructor(options) {
        super();
        this.dsp = null;
        this.stopped = false;
        this.room = options.processorOptions.roomType;
        this.current = new Float32Array(SOURCES * 4);
        this.port.onmessage = ({ data }) => {
            if (data.type === "shutdown") {
                this.stopped = true;
                this.dsp?._WebReverb_Shutdown();
                this.dsp = null;
                this.port.close();
            }
        };
        // The generated module embeds its Wasm: AudioWorklet has no fetch API.
        createReverb().then((dsp) => {
            if (this.stopped) return;
            if (!dsp._WebReverb_Initialize(sampleRate) || !dsp._WebReverb_SetRoom(this.room))
                throw new Error("OpenAL reverb initialization rejected");
            this.dsp = dsp;
            this.input = dsp._WebReverb_Input() >>> 2;
            this.output = dsp._WebReverb_Output() >>> 2;
            this.port.postMessage({ type: "ready", memoryBytes: dsp.HEAPF32.byteLength });
        }).catch((error) => {
            this.port.postMessage({ type: "error", message: String(error?.message ?? error) });
            this.stopped = true;
        });
    }

    process(inputs, outputs, parameters) {
        if (this.stopped) return false;
        if (!this.dsp) return true;
        const room = Math.round(parameters.room[0]);
        if (room !== this.room) {
            if (!this.dsp._WebReverb_SetRoom(room)) throw new Error("Invalid room parameter");
            this.room = room;
        }
        const output = outputs[0], frames = output[0].length;
        if (frames > STRIDE) throw new Error("Audio quantum exceeds reverb buffer");
        const heap = this.dsp.HEAPF32;
        heap.fill(0, this.input, this.input + STRIDE * 4);
        for (let source = 0; source < SOURCES; ++source) {
            const channels = inputs[source], at = source * 4;
            if (!channels.length) {
                this.current.fill(0, at, at + 4);
                continue;
            }
            if (channels.length === 1) {
                for (let c = 0; c < 4; ++c) {
                    const from = this.current[at + c], to = parameters[PARAMS[source][c]][0];
                    if (from === 0 && to === 0) continue;
                    const step = (to - from) / frames, base = this.input + c * STRIDE;
                    for (let i = 0; i < frames; ++i)
                        heap[base + i] += channels[0][i] * (from + step * i);
                }
            } else {
                // Native non-spatial stereo maps L/R to +/-90 degrees in
                // Pairwise mode; preserve both channels in the wet bus.
                const from = this.current[at], to = parameters[PARAMS[source][0]][0];
                if (from !== 0 || to !== 0) {
                    const step = (to - from) / frames;
                    for (let i = 0; i < frames; ++i) {
                        const gain = from + step * i;
                        heap[this.input + i] += (channels[0][i] + channels[1][i]) * gain;
                        heap[this.input + STRIDE + i] += (channels[0][i] - channels[1][i]) * SQRT3 * gain;
                    }
                }
            }
            for (let c = 0; c < 4; ++c) this.current[at + c] = parameters[PARAMS[source][c]][0];
        }
        if (!this.dsp._WebReverb_Process(frames)) throw new Error("Invalid reverb PCM input");
        for (let c = 0; c < 2; ++c)
            for (let i = 0; i < frames; ++i) output[c][i] = heap[this.output + c * STRIDE + i];
        return true;
    }
}

registerProcessor("kisak-reverb", KisakReverbProcessor);
