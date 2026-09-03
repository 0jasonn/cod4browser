// Main-thread Web Audio owner for the Worker-side OpenAL compatibility proxy.
// This class owns only Web Audio resources. Canonical SND channel/alias state
// and playback timing remain in the Kisak Worker.

export const WEB_AUDIO_PROTOCOL_VERSION = 1;
const MAX_SOURCES = 54; // 53 canonical SND channels plus one cinematic track.
const MAX_BUFFERS = 512;
const MAX_PCM_BYTES = 16 * 1024 * 1024;
const MAX_DECODED_PCM_BYTES = 64 * 1024 * 1024;
const MAX_QUEUED_BUFFERS_PER_SOURCE = 128;

function clamp(value, low, high) {
    return Math.min(high, Math.max(low, Number.isFinite(value) ? value : low));
}

// Device coefficients from the RBJ/W3C Audio EQ Cookbook (Q form):
// https://www.w3.org/TR/audio-eq-cookbook/
// IIRFilterNode preserves Q for shelves too; BiquadFilterNode ignores it there.
function eqCoefficients(type, gain, frequency, q, sampleRate) {
    const a = 10 ** (gain / 40);
    if (!Number.isFinite(a) || a <= 0) throw new Error("Unrepresentable EQ gain");
    if (frequency === 0 || frequency >= sampleRate / 2) {
        const atZero = frequency === 0;
        const level = type === 0 ? (atZero ? 0 : 1)
            : type === 1 ? (atZero ? 1 : 0)
            : type === 2 ? (atZero ? 1 : a * a)
            : type === 3 ? (atZero ? a * a : 1) : 1;
        if (!Number.isFinite(level)) throw new Error("Unrepresentable EQ gain");
        return { level };
    }
    const w = 2 * Math.PI * frequency / sampleRate;
    const c = Math.cos(w);
    const alpha = Math.sin(w) / (2 * q);
    const root = 2 * Math.sqrt(a) * alpha;
    let b, d;
    switch (type) {
    case 0:
        b = [(1 - c) / 2, 1 - c, (1 - c) / 2];
        d = [1 + alpha, -2 * c, 1 - alpha]; break;
    case 1:
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2];
        d = [1 + alpha, -2 * c, 1 - alpha]; break;
    case 2:
        b = [a * (a + 1 - (a - 1) * c + root),
            2 * a * (a - 1 - (a + 1) * c), a * (a + 1 - (a - 1) * c - root)];
        d = [a + 1 + (a - 1) * c + root,
            -2 * (a - 1 + (a + 1) * c), a + 1 + (a - 1) * c - root]; break;
    case 3:
        b = [a * (a + 1 + (a - 1) * c + root),
            -2 * a * (a - 1 + (a + 1) * c), a * (a + 1 + (a - 1) * c - root)];
        d = [a + 1 - (a - 1) * c + root,
            2 * (a - 1 - (a + 1) * c), a + 1 - (a - 1) * c - root]; break;
    default:
        b = [1 + alpha * a, -2 * c, 1 - alpha * a];
        d = [1 + alpha / a, -2 * c, 1 - alpha / a]; break;
    }
    b = b.map((value) => value / d[0]);
    d = d.map((value) => value / d[0]);
    // Reject degenerate coefficients before publishing any part of a chain.
    if (![...b, ...d].every(Number.isFinite) || Math.abs(d[2]) >= 1 ||
        1 + d[1] + d[2] <= 0 || 1 - d[1] + d[2] <= 0)
        throw new Error("Unstable EQ coefficients");
    return { b, d };
}

export class WebAudioDriver {
    /**
     * @param {{contextFactory?: () => AudioContext | null,
     *   onDiagnostic?: (message: string) => void,
     *   onPlaybackStarted?: (detail: object) => void,
     *   onTelemetry?: (detail: object) => void,
     *   startMuted?: boolean,
     *   decodedPcmBudgetBytes?: number,
     *   maxQueuedBuffersPerSource?: number}} [options]
     */
    constructor({
        contextFactory,
        onDiagnostic,
        onPlaybackStarted,
        onTelemetry,
        startMuted = true,
        decodedPcmBudgetBytes = MAX_DECODED_PCM_BYTES,
        maxQueuedBuffersPerSource = MAX_QUEUED_BUFFERS_PER_SOURCE,
    } = {}) {
        this.contextFactory = contextFactory ?? (() => {
            const Context = globalThis.AudioContext ?? globalThis.webkitAudioContext;
            return Context ? new Context() : null;
        });
        this.onDiagnostic = onDiagnostic;
        this.onPlaybackStarted = onPlaybackStarted;
        this.onTelemetry = onTelemetry;
        this.decodedPcmBudgetBytes = decodedPcmBudgetBytes;
        this.maxQueuedBuffersPerSource = maxQueuedBuffersPerSource;
        this.context = null;
        this.sources = new Map();
        this.buffers = new Map();
        this.bufferMetadata = new Map();
        this.decodedPcmBytes = 0;
        this.telemetry = { underruns: 0, overruns: 0, evictions: 0 };
        this.audioUnlocked = !startMuted;
        this.gestureTarget = null;
        this.gestureHandler = null;
        this.roomType = 0;
        this.reverbNode = null;
        this.reverbReady = null;
        this.reverbModule = null;
        this.reverbEpoch = 0;
        this.reverbMemoryBytes = 0;
    }

    diagnostic(message) {
        this.onDiagnostic?.(String(message));
    }

    publishTelemetry() {
        const queuedBufferCount = [...this.sources.values()].reduce(
            (total, source) => total + source.queue.length, 0);
        const detail = Object.freeze({
            decodedPcmBytes: this.decodedPcmBytes,
            decodedPcmBudgetBytes: this.decodedPcmBudgetBytes,
            bufferCount: this.buffers.size,
            sourceCount: this.sources.size,
            queuedBufferCount,
            reverbReady: Boolean(this.reverbNode),
            reverbRoomType: this.roomType,
            reverbMemoryBytes: this.reverbMemoryBytes,
            ...this.telemetry,
        });
        this.onTelemetry?.(detail);
        globalThis.dispatchEvent?.(new CustomEvent("kisakcod:audio-telemetry", { detail }));
        return detail;
    }

    ensureContext() {
        if (this.context) return this.context;
        try {
            this.context = this.contextFactory();
        } catch (error) {
            this.diagnostic(`Web Audio unavailable: ${error?.message ?? error}`);
            return null;
        }
        if (!this.context) {
            this.diagnostic("Web Audio unavailable; loaded sounds will be muted.");
        } else if (!this.audioUnlocked && this.context.state === "running") {
            void this.context.suspend?.();
        }
        return this.context;
    }

    /** @param {EventTarget} [target] */
    attachGestureResume(target = globalThis) {
        if (this.gestureTarget || !target?.addEventListener) return;
        this.gestureTarget = target;
        this.gestureHandler = () => { void this.resumeFromGesture(); };
        for (const type of ["pointerdown", "keydown", "touchstart"]) {
            target.addEventListener(type, this.gestureHandler, { passive: true });
        }
    }

    async resumeFromGesture() {
        this.audioUnlocked = true;
        const context = this.ensureContext();
        if (!context?.resume) return false;
        for (const source of this.sources.values()) this.applyProperties(source);
        try {
            await context.resume();
            const resumed = context.state === undefined || context.state === "running";
            if (!resumed) {
                this.audioUnlocked = false;
                for (const source of this.sources.values()) this.applyProperties(source);
            }
            return resumed;
        } catch (error) {
            this.audioUnlocked = false;
            for (const source of this.sources.values()) this.applyProperties(source);
            this.diagnostic(`Web Audio resume failed: ${error?.message ?? error}`);
            return false;
        }
    }

    closeReverbNode(node) {
        if (!node) return;
        node.port.postMessage({ type: "shutdown" });
        node.disconnect();
        node.port.close();
    }

    ensureReverb() {
        if (this.reverbReady) return this.reverbReady;
        const epoch = this.reverbEpoch, context = this.ensureContext();
        this.reverbReady = (async () => {
            let node;
            try {
                if (!context?.audioWorklet || !globalThis.AudioWorkletNode)
                    throw new Error("AudioWorklet is unavailable");
                this.reverbModule ??= context.audioWorklet.addModule(
                    new URL("./web_reverb_worklet.mjs", import.meta.url));
                await this.reverbModule;
                if (epoch !== this.reverbEpoch || context !== this.context) return null;
                node = new AudioWorkletNode(context, "kisak-reverb", {
                    numberOfInputs: MAX_SOURCES, numberOfOutputs: 1,
                    outputChannelCount: [2], channelCountMode: "max",
                    channelInterpretation: "discrete",
                    processorOptions: { roomType: this.roomType },
                    parameterData: { room: this.roomType },
                });
                const memoryBytes = await new Promise((resolve, reject) => {
                    const timer = setTimeout(() => reject(new Error("DSP startup timed out")), 5000);
                    node.port.onmessage = ({ data }) => {
                        if (data.type === "ready") { clearTimeout(timer); resolve(data.memoryBytes); }
                        else if (data.type === "error") { clearTimeout(timer); reject(new Error(data.message)); }
                    };
                    node.onprocessorerror = () => {
                        clearTimeout(timer);
                        const error = new Error("Reverb processor failed");
                        if (this.reverbNode === node) {
                            this.reverbNode = null;
                            this.reverbMemoryBytes = 0;
                            this.closeReverbNode(node);
                            this.diagnostic(error.message);
                            this.publishTelemetry();
                        }
                        reject(error);
                    };
                });
                if (epoch !== this.reverbEpoch || context !== this.context) {
                    this.closeReverbNode(node);
                    return null;
                }
                this.reverbNode = node;
                this.reverbMemoryBytes = Number(memoryBytes);
                node.parameters.get("room").value = this.roomType;
                node.connect(context.destination);
                for (const source of this.sources.values()) {
                    source.reverbProperties = null;
                    this.applyReverbProperties(source);
                    for (const pcm of [source.node, ...source.streamNodes])
                        if (pcm) this.connectReverb(source, pcm);
                }
                this.publishTelemetry();
                return node;
            } catch (error) {
                this.closeReverbNode(node);
                if (epoch === this.reverbEpoch)
                    this.diagnostic(`Room reverb unavailable: ${error?.message ?? error}`);
                return null;
            }
        })();
        return this.reverbReady;
    }

    applyReverbProperties(source) {
        if (!this.reverbNode) {
            if (source.wet > 0) void this.ensureReverb();
            return;
        }
        const gain = this.audioUnlocked ? source.gain * source.wet : 0;
        let { x, y, z } = source;
        // OpenAL Pairwise stereo's front azimuth expands by 3/2. SND supplies
        // listener-space coordinates and distance attenuation; this is only
        // the device's ACN/N3D encoding for the native reverb input.
        const distance = source.spatialized ? Math.hypot(x, y, z) : 0;
        if (distance > 1.1920929e-7) {
            x /= distance; y /= distance; z /= distance;
            if (z < 0) {
                const horizontal = Math.hypot(x, z);
                const angle = Math.max(-Math.PI / 2, Math.min(Math.PI / 2,
                    1.5 * Math.atan2(x, -z)));
                x = Math.sin(angle) * horizontal;
                z = -Math.cos(angle) * horizontal;
            }
        } else { x = 0; y = 0; z = -1; }
        const values = [gain, -Math.sqrt(3) * x * gain,
            Math.sqrt(3) * y * gain, -Math.sqrt(3) * z * gain];
        if (source.reverbProperties?.every((value, i) => value === values[i])) return;
        source.reverbProperties = values;
        for (let c = 0; c < 4; ++c)
            this.reverbNode.parameters.get(`s${source.sourceId - 1}_${c}`).value = values[c];
    }

    connectReverb(source, pcm) {
        if (this.reverbNode)
            (source.eqNodes.at(-1) ?? pcm).connect(this.reverbNode, 0, source.sourceId - 1);
    }

    validId(value, maximum) {
        return Number.isInteger(value) && value > 0 && value <= maximum;
    }

    validSource(command) {
        return this.validId(command.sourceId, MAX_SOURCES);
    }

    handleCommand(command) {
        if (!command || command.version !== WEB_AUDIO_PROTOCOL_VERSION ||
            typeof command.op !== "string") {
            this.diagnostic("Ignored malformed Web Audio command.");
            return false;
        }
        switch (command.op) {
        case "source-create":
            return this.createSource(command.id);
        case "source-delete":
            return this.deleteSource(command.sourceId ?? command.id);
        case "buffer-delete":
            return this.deleteBuffer(command.id);
        case "buffer-upload":
            return this.uploadBuffer(command);
        case "device-reset":
            this.resetResources();
            return true;
        case "room-type":
            if (!Number.isInteger(command.id) || command.id < 0 || command.id >= 26) return false;
            this.roomType = command.id;
            if (this.reverbNode) this.reverbNode.parameters.get("room").value = command.id;
            else void this.ensureReverb();
            return true;
        case "source-property":
        case "source-play":
        case "source-pause":
        case "source-stop":
            return this.sourceCommand(command);
        case "source-eq":
            return this.setSourceEq(command);
        case "source-queue":
        case "source-unqueue":
            return this.queueCommand(command);
        default:
            this.diagnostic(`Ignored unknown Web Audio command: ${command.op}`);
            return false;
        }
    }

    createSource(id) {
        if (!this.validId(id, MAX_SOURCES)) return false;
        if (!this.sources.has(id)) {
            this.sources.set(id, {
                generation: 0, bufferId: 0, gain: 1, pitch: 1, looping: false,
                offset: 0, x: 0, y: 0, z: 0, state: "stopped", node: null,
                gainNode: null, panner: null, startedAt: 0, activeBufferId: 0,
                aliasName: "", sourceId: id, spatialized: false,
                queue: [], queueProcessed: 0, streamNodes: [], queueEndTime: 0,
                eqBands: new Array(30).fill(0), eqNodes: [],
                wet: 0, reverbProperties: null,
            });
        }
        return true;
    }

    cleanup(source) {
        const node = source.node;
        const gainNode = source.gainNode;
        const panner = source.panner;
        const streamNodes = source.streamNodes.splice(0);
        source.node = source.gainNode = source.panner = null;
        for (const entry of source.queue) entry.node = null;
        try { node?.stop(); } catch {}
        for (const streamNode of streamNodes) {
            try { streamNode.stop?.(); } catch {}
            streamNode.disconnect?.();
        }
        node?.disconnect?.();
        gainNode?.disconnect?.();
        panner?.disconnect?.();
        for (const filter of source.eqNodes) filter.disconnect();
        source.eqNodes = [];
        source.queueEndTime = 0;
    }

    setSourceEq(command) {
        const source = this.sources.get(command.sourceId);
        const bands = command.bands;
        if (!this.validSource(command) || !source ||
            !Number.isInteger(command.generation) || command.generation < 0 ||
            !Array.isArray(bands) || bands.length !== 30) return false;
        if (command.generation < source.generation) return true;
        for (let i = 0; i < 30; i += 5) {
            if ((bands[i] !== 0 && bands[i] !== 1) || (bands[i] && (
                !Number.isInteger(bands[i + 1]) || bands[i + 1] < 0 || bands[i + 1] > 4 ||
                !Number.isFinite(bands[i + 2]) || !Number.isFinite(bands[i + 3]) ||
                bands[i + 3] < 0 || bands[i + 3] > 20000 ||
                !Number.isFinite(bands[i + 4]) || bands[i + 4] <= 0))) return false;
        }
        if (bands.every((value, index) => value === source.eqBands[index])) return true;
        if (!this.replaceEqGraph(source, bands)) return false;
        source.eqBands = bands.slice();
        source.generation = command.generation;
        return true;
    }

    replaceEqGraph(source, bands = source.eqBands) {
        // ponytail: changed IIR coefficients reset history; retain DSP history
        // if authored sweeps reveal audible update transients.
        const context = this.context;
        const filters = [];
        try {
            for (let i = 0; context && i < 30; i += 5) {
                if (!bands[i]) continue;
                const coefficients = eqCoefficients(...bands.slice(i + 1, i + 5), context.sampleRate);
                const filter = coefficients.level === undefined
                    ? context.createIIRFilter(coefficients.b, coefficients.d) : context.createGain();
                if (coefficients.level !== undefined) filter.gain.value = coefficients.level;
                filters.push(filter);
            }
        } catch (error) {
            for (const filter of filters) filter.disconnect();
            this.diagnostic(`Rejected Web Audio EQ: ${error?.message ?? error}`);
            return false;
        }
        const output = source.panner ?? source.gainNode ?? context?.destination;
        for (let i = 0; i < filters.length; ++i) filters[i].connect(filters[i + 1] ?? output);
        for (const filter of source.eqNodes) filter.disconnect();
        source.eqNodes = filters;
        for (const node of [source.node, ...source.streamNodes]) {
            if (!node) continue;
            node.disconnect();
            this.connectToOutput(source, node, context);
        }
        return true;
    }

    deleteSource(id) {
        if (!this.validId(id, MAX_SOURCES)) return false;
        const source = this.sources.get(id);
        if (!source) return true;
        source.generation++;
        this.cleanup(source);
        this.sources.delete(id);
        return true;
    }

    deleteBuffer(id) {
        if (!this.validId(id, MAX_BUFFERS)) return false;
        this.decodedPcmBytes -= this.bufferMetadata.get(id)?.bytes ?? 0;
        this.bufferMetadata.delete(id);
        this.buffers.delete(id);
        for (const source of this.sources.values()) {
            if (source.activeBufferId === id || source.bufferId === id) {
                source.generation++;
                this.cleanup(source);
                source.state = "stopped";
                source.offset = 0;
                source.activeBufferId = 0;
            }
            if (source.bufferId === id) source.bufferId = 0;
        }
        this.publishTelemetry();
        return true;
    }

    bufferReferenced(id) {
        for (const source of this.sources.values()) {
            if (source.bufferId === id || source.activeBufferId === id ||
                source.queue.some((entry) => entry.bufferId === id)) return true;
        }
        return false;
    }

    makeDecodedRoom(bytes, replacingId) {
        const replacedBytes = this.bufferMetadata.get(replacingId)?.bytes ?? 0;
        let projected = this.decodedPcmBytes - replacedBytes + bytes;
        if (projected <= this.decodedPcmBudgetBytes) return true;
        const candidates = [...this.bufferMetadata]
            .filter(([id]) => id !== replacingId && !this.bufferReferenced(id))
            .sort((left, right) => left[1].lastUsed - right[1].lastUsed);
        for (const [id, metadata] of candidates) {
            this.buffers.delete(id);
            this.bufferMetadata.delete(id);
            this.decodedPcmBytes -= metadata.bytes;
            projected -= metadata.bytes;
            ++this.telemetry.evictions;
            if (projected <= this.decodedPcmBudgetBytes) break;
        }
        if (projected > this.decodedPcmBudgetBytes) {
            ++this.telemetry.overruns;
            this.publishTelemetry();
            return false;
        }
        return true;
    }

    uploadBuffer(command) {
        if (!this.validId(command.bufferId, MAX_BUFFERS) ||
            !(command.pcm instanceof ArrayBuffer) || command.pcm.byteLength === 0 ||
            command.pcm.byteLength > MAX_PCM_BYTES || !Number.isInteger(command.rate) ||
            command.rate <= 0 || (command.format !== 0x1101 && command.format !== 0x1103)) {
            this.diagnostic("Rejected invalid Web Audio PCM upload.");
            return false;
        }
        if (command.pcm.byteLength !== command.bytes) {
            this.diagnostic("Rejected Web Audio PCM size mismatch.");
            return false;
        }
        const context = this.ensureContext();
        if (!context?.createBuffer) return false;
        const channels = command.format === 0x1103 ? 2 : 1;
        if (command.pcm.byteLength % (channels * 2) !== 0) return false;
        const frames = command.pcm.byteLength / (channels * 2);
        try {
            const buffer = context.createBuffer(channels, frames, command.rate);
            const samples = new Int16Array(command.pcm);
            for (let channel = 0; channel < channels; ++channel) {
                const output = buffer.getChannelData(channel);
                for (let frame = 0; frame < frames; ++frame) {
                    const sample = samples[frame * channels + channel];
                    output[frame] = sample / 32768;
                }
            }
            const decodedBytes = frames * channels * Float32Array.BYTES_PER_ELEMENT;
            if (!this.makeDecodedRoom(decodedBytes, command.bufferId)) {
                this.diagnostic("Rejected Web Audio PCM upload: decoded-audio budget exhausted.");
                return false;
            }
            this.decodedPcmBytes -= this.bufferMetadata.get(command.bufferId)?.bytes ?? 0;
            this.buffers.set(command.bufferId, buffer);
            this.bufferMetadata.set(command.bufferId, {
                bytes: decodedBytes,
                lastUsed: performance.now(),
            });
            this.decodedPcmBytes += decodedBytes;
            this.publishTelemetry();
            return true;
        } catch (error) {
            this.diagnostic(`Web Audio PCM upload failed: ${error?.message ?? error}`);
            return false;
        }
    }

    applyProperties(source) {
        this.applyReverbProperties(source);
        if (source.gainNode)
            source.gainNode.gain.value = this.audioUnlocked ? source.gain : 0;
        if (source.node) source.node.playbackRate.value = source.pitch;
        for (const node of source.streamNodes)
            node.playbackRate.value = source.pitch;
        if (source.panner?.positionX) {
            source.panner.positionX.value = source.x;
            source.panner.positionY.value = source.y;
            source.panner.positionZ.value = source.z;
        } else if (source.panner?.setPosition) {
            source.panner.setPosition(source.x, source.y, source.z);
        }
    }

    createOutputGraph(source, context) {
        const gainNode = context.createGain?.();
        const panner = source.spatialized ? context.createPanner?.() : null;
        if (gainNode)
            gainNode.gain.value = this.audioUnlocked ? source.gain : 0;
        if (panner) {
            panner.panningModel = "equalpower";
            panner.distanceModel = "inverse";
            panner.refDistance = 1;
            panner.maxDistance = 100000;
            panner.rolloffFactor = 0;
            panner.connect(gainNode ?? context.destination);
        }
        if (gainNode) gainNode.connect(context.destination);
        source.gainNode = gainNode;
        source.panner = panner;
        this.replaceEqGraph(source);
        this.applyProperties(source);
    }

    connectToOutput(source, node, context) {
        if (source.eqNodes.length) node.connect(source.eqNodes[0]);
        else if (source.panner) node.connect(source.panner);
        else if (source.gainNode) node.connect(source.gainNode);
        else node.connect?.(context.destination);
        this.connectReverb(source, node);
    }

    startSource(source, generation) {
        const context = this.ensureContext();
        const buffer = this.buffers.get(source.bufferId);
        if (!context || !buffer || !context.createBufferSource) {
            ++this.telemetry.underruns;
            this.publishTelemetry();
            this.cleanup(source);
            source.activeBufferId = 0;
            source.state = "stopped";
            return false;
        }
        const metadata = this.bufferMetadata.get(source.bufferId);
        if (metadata) metadata.lastUsed = performance.now();
        this.cleanup(source);
        const node = context.createBufferSource();
        node.buffer = buffer;
        node.loop = source.looping;
        node.playbackRate.value = source.pitch;
        // Native OpenAL plays ordinary 2D/stereo sources directly. Routing
        // them through a PannerNode downmixes stereo to mono and can phase-
        // cancel first-person weapon layers. Only sources positioned by the
        // canonical 3D driver should enter the spatialization graph.
        this.createOutputGraph(source, context);
        this.connectToOutput(source, node, context);
        const gainNode = source.gainNode;
        const panner = source.panner;
        const capturedGeneration = generation;
        node.onended = () => {
            if (source.generation !== capturedGeneration || source.node !== node) return;
            node.disconnect?.();
            gainNode?.disconnect?.();
            panner?.disconnect?.();
            for (const filter of source.eqNodes) filter.disconnect();
            source.eqNodes = [];
            source.state = source.looping ? "playing" : "stopped";
            source.activeBufferId = 0;
            source.node = source.gainNode = source.panner = null;
        };
        source.node = node;
        source.gainNode = gainNode;
        source.panner = panner;
        source.activeBufferId = source.bufferId;
        source.state = "playing";
        source.startedAt = context.currentTime ?? 0;
        try { node.start(0, clamp(source.offset, 0, buffer.duration)); }
        catch (error) {
            this.cleanup(source); source.activeBufferId = 0;
            source.state = "stopped"; this.diagnostic(error); return false;
        }
        try {
            this.onPlaybackStarted?.({
                sourceId: source.sourceId,
                generation,
                bufferId: source.activeBufferId,
                aliasName: source.aliasName,
                spatialized: source.spatialized,
                position: { x: source.x, y: source.y, z: source.z },
                gain: source.gain,
                pitch: source.pitch,
                contextState: context.state ?? "unknown",
            });
        } catch {}
        return true;
    }

    scheduleQueuedBuffers(source, generation, resume = false) {
        const context = this.ensureContext();
        if (!context?.createBufferSource) return false;
        let when = Math.max(context.currentTime ?? 0, source.queueEndTime || 0);
        let scheduled = false;
        for (let index = source.queueProcessed; index < source.queue.length; ++index) {
            const entry = source.queue[index];
            if (entry.node || entry.completed) continue;
            const buffer = this.buffers.get(entry.bufferId);
            if (!buffer) {
                ++this.telemetry.underruns;
                this.publishTelemetry();
                return false;
            }
            const metadata = this.bufferMetadata.get(entry.bufferId);
            if (metadata) metadata.lastUsed = performance.now();
            const node = context.createBufferSource();
            node.buffer = buffer;
            node.loop = false;
            node.playbackRate.value = source.pitch;
            this.connectToOutput(source, node, context);
            const offset = resume && index === source.queueProcessed
                ? clamp(source.offset, 0, buffer.duration) : 0;
            const duration = Math.max(0,
                (buffer.duration - offset) / Math.max(0.001, source.pitch));
            entry.node = node;
            entry.startTime = when;
            entry.endTime = when + duration;
            source.streamNodes.push(node);
            const capturedGeneration = generation;
            node.onended = () => {
                node.disconnect?.();
                const at = source.streamNodes.indexOf(node);
                if (at >= 0) source.streamNodes.splice(at, 1);
                if (source.generation === capturedGeneration && entry.node === node) {
                    entry.node = null;
                    entry.completed = true;
                }
            };
            try { node.start(when, offset); }
            catch (error) {
                node.disconnect?.();
                entry.node = null;
                this.diagnostic(error);
                return false;
            }
            when += duration;
            scheduled = true;
        }
        source.queueEndTime = when;
        return scheduled || source.streamNodes.length > 0;
    }

    startQueuedSource(source, generation) {
        const context = this.ensureContext();
        if (!context || source.queueProcessed >= source.queue.length) return false;
        this.cleanup(source);
        this.createOutputGraph(source, context);
        source.state = "playing";
        source.startedAt = context.currentTime ?? 0;
        source.queueEndTime = source.startedAt;
        if (!this.scheduleQueuedBuffers(source, generation, true)) {
            this.cleanup(source);
            source.state = "stopped";
            return false;
        }
        source.activeBufferId = source.queue[source.queueProcessed]?.bufferId ?? 0;
        try {
            this.onPlaybackStarted?.({
                sourceId: source.sourceId,
                generation,
                bufferId: source.activeBufferId,
                aliasName: source.aliasName,
                spatialized: source.spatialized,
                position: { x: source.x, y: source.y, z: source.z },
                gain: source.gain,
                pitch: source.pitch,
                streaming: true,
                contextState: context.state ?? "unknown",
            });
        } catch {}
        return true;
    }

    queueCommand(command) {
        if (!this.validSource(command) || !this.createSource(command.sourceId) ||
            !Number.isInteger(command.generation) ||
            !Array.isArray(command.bufferIds) ||
            command.bufferIds.some((id) => !this.validId(id, MAX_BUFFERS))) {
            this.diagnostic("Rejected malformed Web Audio stream queue command.");
            return false;
        }
        const source = this.sources.get(command.sourceId);
        if (command.generation < source.generation) return true;
        if (command.op === "source-queue") {
            if (source.queue.length + command.bufferIds.length >
                this.maxQueuedBuffersPerSource) {
                ++this.telemetry.overruns;
                this.publishTelemetry();
                this.diagnostic("Rejected Web Audio stream queue: source queue limit exceeded.");
                return false;
            }
            if (command.bufferIds.some((id) => !this.buffers.has(id))) return false;
            source.generation = command.generation;
            for (const bufferId of command.bufferIds)
                source.queue.push({ bufferId, node: null, completed: false });
            if (source.state === "playing")
                return this.scheduleQueuedBuffers(source, source.generation);
            return true;
        }
        if (command.bufferIds.length > source.queue.length ||
            command.bufferIds.some((bufferId, index) =>
                source.queue[index].bufferId !== bufferId)) {
            this.diagnostic("Rejected out-of-order Web Audio stream unqueue.");
            return false;
        }
        source.generation = command.generation;
        const removed = source.queue.splice(0, command.bufferIds.length);
        for (const entry of removed) {
            const node = entry.node;
            if (node) {
                try { node.stop?.(); } catch {}
                node.disconnect?.();
                const at = source.streamNodes.indexOf(node);
                if (at >= 0) source.streamNodes.splice(at, 1);
            }
        }
        source.queueProcessed = Math.max(0,
            source.queueProcessed - command.bufferIds.length);
        source.activeBufferId = source.queue[source.queueProcessed]?.bufferId ?? 0;
        return true;
    }

    sourceCommand(command) {
        if (!this.validSource(command) || !this.createSource(command.sourceId)) return false;
        if (!Number.isInteger(command.generation)) {
            this.diagnostic("Rejected Web Audio source command without a generation.");
            return false;
        }
        const source = this.sources.get(command.sourceId);
        if (command.generation < source.generation)
            return true;
        const hasPosition = command.x !== undefined || command.y !== undefined ||
            command.z !== undefined;
        if (hasPosition && !(Number.isFinite(command.x) &&
            Number.isFinite(command.y) && Number.isFinite(command.z))) {
            this.diagnostic("Rejected Web Audio source command with an invalid position.");
            return false;
        }
        if (command.wet !== undefined && (!Number.isFinite(command.wet) ||
            command.wet < 0 || command.wet > 1)) return false;
        if (command.wet !== undefined) source.wet = command.wet;
        if (Number.isFinite(command.gain)) source.gain = clamp(command.gain, 0, 16);
        if (Number.isFinite(command.pitch)) source.pitch = clamp(command.pitch, 0.001, 16);
        if (Number.isFinite(command.offset)) source.offset = Math.max(0, command.offset);
        if (hasPosition) [source.x, source.y, source.z] = [command.x, command.y, command.z];
        if (typeof command.looping === "boolean") source.looping = command.looping;
        if (typeof command.spatialized === "boolean")
            source.spatialized = command.spatialized;
        if (Number.isInteger(command.queueProcessed))
            source.queueProcessed = clamp(command.queueProcessed, 0, source.queue.length);
        if (Number.isInteger(command.bufferId)) source.bufferId = command.bufferId;
        if (typeof command.aliasName === "string")
            source.aliasName = command.aliasName.slice(0, 128);
        if (Number.isInteger(command.generation)) source.generation = command.generation;
        if (command.op === "source-property") { this.applyProperties(source); return true; }
        if (command.op === "source-play") {
            if (source.queue.length > source.queueProcessed)
                return this.startQueuedSource(source, source.generation);
            return this.startSource(source, source.generation);
        }
        if (command.op === "source-pause") {
            if (source.state === "playing" && source.node && source.queue.length === 0) {
                const current = this.context?.currentTime ?? source.startedAt;
                source.offset += Math.max(0, current - source.startedAt) * source.pitch;
            }
            this.cleanup(source); source.activeBufferId = 0;
            source.state = "paused"; return true;
        }
        this.cleanup(source); source.offset = 0; source.activeBufferId = 0;
        source.state = "stopped"; return true;
    }

    resetResources() {
        ++this.reverbEpoch;
        this.closeReverbNode(this.reverbNode);
        this.reverbNode = this.reverbReady = null;
        this.reverbMemoryBytes = 0;
        this.roomType = 0;
        for (const source of this.sources.values()) {
            source.generation++;
            this.cleanup(source);
        }
        this.sources.clear();
        this.buffers.clear();
        this.bufferMetadata.clear();
        this.decodedPcmBytes = 0;
        this.publishTelemetry();
    }

    dispose() {
        this.resetResources();
        if (this.gestureTarget && this.gestureHandler) {
            for (const type of ["pointerdown", "keydown", "touchstart"])
                this.gestureTarget.removeEventListener(type, this.gestureHandler);
        }
        this.gestureTarget = this.gestureHandler = null;
        const context = this.context;
        this.context = null;
        this.reverbModule = null;
        void context?.close?.();
    }
}
