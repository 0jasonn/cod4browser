// Main-thread Web Audio owner for the Worker-side OpenAL compatibility proxy.
// This class owns only Web Audio resources. Canonical SND channel/alias state
// and playback timing remain in the Kisak Worker.

export const WEB_AUDIO_PROTOCOL_VERSION = 1;
const MAX_SOURCES = 53;
const MAX_BUFFERS = 512;
const MAX_PCM_BYTES = 16 * 1024 * 1024;
const MAX_DECODED_PCM_BYTES = 64 * 1024 * 1024;
const MAX_QUEUED_BUFFERS_PER_SOURCE = 128;

function clamp(value, low, high) {
    return Math.min(high, Math.max(low, Number.isFinite(value) ? value : low));
}

export class WebAudioDriver {
    /**
     * @param {{contextFactory?: () => AudioContext | null,
     *   onDiagnostic?: (message: string) => void,
     *   onPlaybackStarted?: (detail: object) => void,
     *   onTelemetry?: (detail: object) => void,
     *   decodedPcmBudgetBytes?: number,
     *   maxQueuedBuffersPerSource?: number}} [options]
     */
    constructor({
        contextFactory,
        onDiagnostic,
        onPlaybackStarted,
        onTelemetry,
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
        this.gestureTarget = null;
        this.gestureHandler = null;
    }

    diagnostic(message) {
        this.onDiagnostic?.(String(message));
    }

    publishTelemetry() {
        const detail = Object.freeze({
            decodedPcmBytes: this.decodedPcmBytes,
            decodedPcmBudgetBytes: this.decodedPcmBudgetBytes,
            bufferCount: this.buffers.size,
            sourceCount: this.sources.size,
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
        source.queueEndTime = 0;
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
                    output[frame] = sample < 0 ? sample / 32768 : sample / 32767;
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
        if (source.gainNode) source.gainNode.gain.value = source.gain;
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
        if (gainNode) gainNode.gain.value = source.gain;
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
        this.applyProperties(source);
    }

    connectToOutput(source, node, context) {
        if (source.panner) node.connect(source.panner);
        else if (source.gainNode) node.connect(source.gainNode);
        else node.connect?.(context.destination);
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
        source.generation = command.generation;
        if (command.op === "source-queue") {
            if (source.queue.length + command.bufferIds.length >
                this.maxQueuedBuffersPerSource) {
                ++this.telemetry.overruns;
                this.publishTelemetry();
                this.diagnostic("Rejected Web Audio stream queue: source queue limit exceeded.");
                return false;
            }
            if (command.bufferIds.some((id) => !this.buffers.has(id))) return false;
            for (const bufferId of command.bufferIds)
                source.queue.push({ bufferId, node: null, completed: false });
            if (source.state === "playing")
                return this.scheduleQueuedBuffers(source, source.generation);
            return true;
        }
        for (const bufferId of command.bufferIds) {
            const entry = source.queue.shift();
            if (!entry || entry.bufferId !== bufferId) {
                this.diagnostic("Rejected out-of-order Web Audio stream unqueue.");
                return false;
            }
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
        if (Number.isFinite(command.gain)) source.gain = clamp(command.gain, 0, 16);
        if (Number.isFinite(command.pitch)) source.pitch = clamp(command.pitch, 0.001, 16);
        if (Number.isFinite(command.offset)) source.offset = Math.max(0, command.offset);
        if (Number.isFinite(command.x)) [source.x, source.y, source.z] = [command.x, command.y, command.z];
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
        void context?.close?.();
    }
}
