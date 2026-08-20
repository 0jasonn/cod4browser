// Main-thread Web Audio owner for the Worker-side OpenAL compatibility proxy.
// This class owns only Web Audio resources. Canonical SND channel/alias state
// and playback timing remain in the Kisak Worker.

export const WEB_AUDIO_PROTOCOL_VERSION = 1;
const MAX_SOURCES = 53;
const MAX_BUFFERS = 512;
const MAX_PCM_BYTES = 16 * 1024 * 1024;

function clamp(value, low, high) {
    return Math.min(high, Math.max(low, Number.isFinite(value) ? value : low));
}

export class WebAudioDriver {
    constructor({ contextFactory, onDiagnostic } = {}) {
        this.contextFactory = contextFactory ?? (() => {
            const Context = globalThis.AudioContext ?? globalThis.webkitAudioContext;
            return Context ? new Context() : null;
        });
        this.onDiagnostic = onDiagnostic;
        this.context = null;
        this.sources = new Map();
        this.buffers = new Map();
        this.gestureTarget = null;
        this.gestureHandler = null;
    }

    diagnostic(message) {
        this.onDiagnostic?.(String(message));
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
        }
        return this.context;
    }

    attachGestureResume(target = globalThis) {
        if (this.gestureTarget || !target?.addEventListener) return;
        this.gestureTarget = target;
        this.gestureHandler = () => { void this.resumeFromGesture(); };
        for (const type of ["pointerdown", "keydown", "touchstart"]) {
            target.addEventListener(type, this.gestureHandler, { passive: true });
        }
    }

    async resumeFromGesture() {
        const context = this.ensureContext();
        if (!context?.resume) return false;
        try {
            await context.resume();
            return context.state === undefined || context.state === "running";
        } catch (error) {
            this.diagnostic(`Web Audio resume failed: ${error?.message ?? error}`);
            return false;
        }
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
        case "source-property":
        case "source-play":
        case "source-pause":
        case "source-stop":
            return this.sourceCommand(command);
        case "source-queue":
        case "source-unqueue":
            // Streaming is deliberately not advertised by this first slice.
            return this.validSource(command);
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
            });
        }
        return true;
    }

    cleanup(source) {
        const node = source.node;
        const gainNode = source.gainNode;
        const panner = source.panner;
        source.node = source.gainNode = source.panner = null;
        try { node?.stop(); } catch {}
        node?.disconnect?.();
        gainNode?.disconnect?.();
        panner?.disconnect?.();
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
                    output[frame] = sample < 0 ? sample / 32768 : sample / 32767;
                }
            }
            this.buffers.set(command.bufferId, buffer);
            return true;
        } catch (error) {
            this.diagnostic(`Web Audio PCM upload failed: ${error?.message ?? error}`);
            return false;
        }
    }

    applyProperties(source) {
        if (source.gainNode) source.gainNode.gain.value = source.gain;
        if (source.node) source.node.playbackRate.value = source.pitch;
        if (source.panner?.positionX) {
            source.panner.positionX.value = source.x;
            source.panner.positionY.value = source.y;
            source.panner.positionZ.value = source.z;
        } else if (source.panner?.setPosition) {
            source.panner.setPosition(source.x, source.y, source.z);
        }
    }

    startSource(source, generation) {
        const context = this.ensureContext();
        const buffer = this.buffers.get(source.bufferId);
        if (!context || !buffer || !context.createBufferSource) {
            this.cleanup(source);
            source.activeBufferId = 0;
            source.state = "stopped";
            return false;
        }
        this.cleanup(source);
        const node = context.createBufferSource();
        node.buffer = buffer;
        node.loop = source.looping;
        node.playbackRate.value = source.pitch;
        const gainNode = context.createGain?.();
        const panner = context.createPanner?.();
        if (gainNode) gainNode.gain.value = source.gain;
        if (panner) node.connect(panner);
        else if (gainNode) node.connect(gainNode);
        if (panner) {
            panner.panningModel = "equalpower";
            panner.distanceModel = "inverse";
            panner.refDistance = 1;
            panner.maxDistance = 100000;
            panner.rolloffFactor = 0;
            panner.connect(gainNode ?? context.destination);
        }
        if (gainNode) gainNode.connect(context.destination);
        else if (!panner) node.connect?.(context.destination);
        const capturedGeneration = generation;
        node.onended = () => {
            if (source.generation !== capturedGeneration || source.node !== node) return;
            node.disconnect?.();
            gainNode?.disconnect?.();
            panner?.disconnect?.();
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
        if (Number.isFinite(command.gain)) source.gain = clamp(command.gain, 0, 16);
        if (Number.isFinite(command.pitch)) source.pitch = clamp(command.pitch, 0.001, 16);
        if (Number.isFinite(command.offset)) source.offset = Math.max(0, command.offset);
        if (Number.isFinite(command.x)) [source.x, source.y, source.z] = [command.x, command.y, command.z];
        if (typeof command.looping === "boolean") source.looping = command.looping;
        if (Number.isInteger(command.bufferId)) source.bufferId = command.bufferId;
        if (Number.isInteger(command.generation)) source.generation = command.generation;
        if (command.op === "source-property") { this.applyProperties(source); return true; }
        if (command.op === "source-play") return this.startSource(source, source.generation);
        if (command.op === "source-pause") {
            if (source.state === "playing" && source.node) {
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
        for (const source of this.sources.values()) {
            source.generation++;
            this.cleanup(source);
        }
        this.sources.clear();
        this.buffers.clear();
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
        void context?.close?.();
    }
}
