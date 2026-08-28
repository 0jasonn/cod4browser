export const MISSION_ROUTE_SCHEMA_VERSION = 1;

export const MISSION_ROUTE_FAILURE = Object.freeze({
    CANCELED: "ROUTE_CANCELED",
    DIVERGED: "ROUTE_DIVERGED",
    INVALID_ROUTE: "INVALID_ROUTE",
    INVALID_STATE: "INVALID_MISSION_STATE",
    PLAYER_DIED: "PLAYER_DIED",
    STUCK: "PLAYER_STUCK",
    TIMEOUT: "SEGMENT_TIMEOUT",
});

const routeKeys = new Set(["schemaVersion", "map", "segments"]);
const segmentKeys = new Set([
    "targetRegion", "expectedProgression", "maxDurationMs",
    "minimumDurationMs", "stuckTimeoutMs", "divergenceRadius",
    "restartPolicy", "actions",
]);
const progressionKeys = new Set([
    "objectiveHashChanged", "activeObjectives", "doneObjectivesDeltaAtLeast",
    "missionFlagsChanged", "checkpointChanged",
]);
const actionKeys = new Set(["ads", "fire", "jump", "use"]);

export class MissionRouteError extends Error
{
    constructor(code, message, segmentIndex = -1)
    {
        super(message);
        this.name = "MissionRouteError";
        this.code = code;
        this.segmentIndex = segmentIndex;
    }
}

function invalid(message)
{
    throw new MissionRouteError(MISSION_ROUTE_FAILURE.INVALID_ROUTE, message);
}

function object(value, name)
{
    if (!value || typeof value !== "object" || Array.isArray(value))
        invalid(`${name} must be an object`);
    return value;
}

function exactKeys(value, allowed, name)
{
    for (const key of Object.keys(value)) {
        if (!allowed.has(key)) invalid(`${name} contains unknown field ${key}`);
    }
}

function finite(value, name)
{
    if (!Number.isFinite(value)) invalid(`${name} must be finite`);
    return value;
}

function integer(value, minimum, maximum, name)
{
    if (!Number.isInteger(value) || value < minimum || value > maximum)
        invalid(`${name} must be an integer from ${minimum} through ${maximum}`);
    return value;
}

function validateProgression(value, name)
{
    if (value === undefined) return undefined;
    const expectation = object(value, name);
    exactKeys(expectation, progressionKeys, name);
    if (Object.keys(expectation).length === 0)
        invalid(`${name} must contain an expectation`);
    for (const field of [
        "objectiveHashChanged", "missionFlagsChanged", "checkpointChanged",
    ]) {
        if (expectation[field] !== undefined && expectation[field] !== true)
            invalid(`${name}.${field} must be true when present`);
    }
    if (expectation.activeObjectives !== undefined)
        integer(expectation.activeObjectives, 0, 32, `${name}.activeObjectives`);
    if (expectation.doneObjectivesDeltaAtLeast !== undefined) {
        integer(expectation.doneObjectivesDeltaAtLeast, 1, 32,
            `${name}.doneObjectivesDeltaAtLeast`);
    }
    return { ...expectation };
}

function validateActions(value, name)
{
    if (value === undefined) return undefined;
    const actions = object(value, name);
    exactKeys(actions, actionKeys, name);
    for (const [field, enabled] of Object.entries(actions)) {
        if (enabled !== true) invalid(`${name}.${field} must be true when present`);
    }
    return { ...actions };
}

export function parseMissionRoute(value)
{
    const route = object(value, "route");
    exactKeys(route, routeKeys, "route");
    if (route.schemaVersion !== MISSION_ROUTE_SCHEMA_VERSION) {
        invalid(`route.schemaVersion must be ${MISSION_ROUTE_SCHEMA_VERSION}`);
    }
    if (typeof route.map !== "string" || !/^[a-z0-9_]+$/.test(route.map) ||
        route.map.startsWith("mp_") || route.map.endsWith("_mp")) {
        invalid("route.map must name one lowercase single-player zone");
    }
    if (!Array.isArray(route.segments) || route.segments.length < 1 ||
        route.segments.length > 1024) {
        invalid("route.segments must contain 1 through 1024 segments");
    }

    const segments = route.segments.map((candidate, index) => {
        const name = `route.segments[${index}]`;
        const segment = object(candidate, name);
        exactKeys(segment, segmentKeys, name);
        const region = object(segment.targetRegion, `${name}.targetRegion`);
        exactKeys(region, new Set(["x", "y", "z", "radius"]),
            `${name}.targetRegion`);
        const targetRegion = {
            x: finite(region.x, `${name}.targetRegion.x`),
            y: finite(region.y, `${name}.targetRegion.y`),
            z: finite(region.z, `${name}.targetRegion.z`),
            radius: finite(region.radius, `${name}.targetRegion.radius`),
        };
        if (targetRegion.radius <= 0 || targetRegion.radius > 65_536)
            invalid(`${name}.targetRegion.radius must be greater than 0 and at most 65536`);
        const maxDurationMs = integer(
            segment.maxDurationMs, 100, 600_000, `${name}.maxDurationMs`);
        const minimumDurationMs = segment.minimumDurationMs === undefined ? 0 :
            integer(segment.minimumDurationMs, 0, maxDurationMs,
                `${name}.minimumDurationMs`);
        const stuckTimeoutMs = segment.stuckTimeoutMs === undefined
            ? Math.min(10_000, maxDurationMs)
            : integer(segment.stuckTimeoutMs, 100, maxDurationMs,
                `${name}.stuckTimeoutMs`);
        let divergenceRadius;
        if (segment.divergenceRadius !== undefined) {
            divergenceRadius = finite(
                segment.divergenceRadius, `${name}.divergenceRadius`);
            if (divergenceRadius <= targetRegion.radius)
                invalid(`${name}.divergenceRadius must exceed the target radius`);
        }
        const restartPolicy = segment.restartPolicy ?? "fail";
        if (!["fail", "resume"].includes(restartPolicy))
            invalid(`${name}.restartPolicy must be fail or resume`);
        return {
            targetRegion,
            maxDurationMs,
            ...(minimumDurationMs === 0 ? {} : { minimumDurationMs }),
            stuckTimeoutMs,
            restartPolicy,
            ...(divergenceRadius === undefined ? {} : { divergenceRadius }),
            ...(segment.expectedProgression === undefined ? {} : {
                expectedProgression: validateProgression(
                    segment.expectedProgression, `${name}.expectedProgression`),
            }),
            ...(segment.actions === undefined ? {} : {
                actions: validateActions(segment.actions, `${name}.actions`),
            }),
        };
    });
    return { schemaVersion: MISSION_ROUTE_SCHEMA_VERSION, map: route.map, segments };
}

function progressionChanged(expectation, before, after)
{
    if (!expectation) return true;
    if (expectation.objectiveHashChanged &&
        after.objectiveHash === before.objectiveHash) return false;
    if (expectation.missionFlagsChanged &&
        after.missionFlags === before.missionFlags) return false;
    if (expectation.activeObjectives !== undefined &&
        after.activeObjectives !== expectation.activeObjectives) return false;
    if (expectation.doneObjectivesDeltaAtLeast !== undefined &&
        after.doneObjectives - before.doneObjectives <
            expectation.doneObjectivesDeltaAtLeast) return false;
    if (expectation.checkpointChanged &&
        after.checkpoint.saveId === before.checkpoint.saveId &&
        after.checkpoint.checksum === before.checkpoint.checksum) return false;
    return true;
}

function angleDelta(target, current)
{
    let delta = (target - current) % 360;
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;
    return delta;
}

function clamp(value, minimum, maximum)
{
    return Math.max(minimum, Math.min(maximum, value));
}

function distanceTo(region, origin)
{
    return Math.hypot(
        region.x - origin[0], region.y - origin[1], region.z - origin[2]);
}

function routeState(value, segmentIndex)
{
    const validVector = (vector) => Array.isArray(vector) && vector.length >= 2 &&
        vector.every(Number.isFinite);
    if (!value || typeof value !== "object" ||
        !Number.isFinite(value.timestampMs) || !validVector(value.origin) ||
        value.origin.length < 3 || !validVector(value.viewAngles) ||
        (value.aimOrigin !== undefined &&
            (!validVector(value.aimOrigin) || value.aimOrigin.length < 3)) ||
        !Number.isFinite(value.health) || !value.progression ||
        !["objectiveHash", "activeObjectives", "doneObjectives", "missionFlags"]
            .every((field) => Number.isInteger(value.progression[field]))) {
        throw new MissionRouteError(
            MISSION_ROUTE_FAILURE.INVALID_STATE,
            "diagnostic mission observation is incomplete or non-finite",
            segmentIndex);
    }
    return value;
}

async function releaseInput(adapter, held)
{
    for (const key of held) await adapter.key(key, false);
    held.clear();
}

async function setHeld(adapter, held, key, down)
{
    if (down === held.has(key)) return;
    await adapter.key(key, down);
    if (down) held.add(key);
    else held.delete(key);
}

/**
 * The adapter deliberately has only read observation and normal input methods.
 * No command, teleport, or gameplay-state setter crosses this boundary.
 */
export function createMissionRouteController(adapter, options = {})
{
    for (const method of ["observe", "key", "mouse", "wait"]) {
        if (typeof adapter?.[method] !== "function")
            throw new TypeError(`mission route adapter requires ${method}()`);
    }
    const tickMs = options.tickMs ?? 100;
    const mouseCountsPerDegree = options.mouseCountsPerDegree ?? 1;
    const maximumMouseDelta = options.maximumMouseDelta ?? 64;
    const minimumProgress = options.minimumProgress ?? 8;
    if (![tickMs, mouseCountsPerDegree, maximumMouseDelta, minimumProgress]
        .every((value) => Number.isFinite(value) && value > 0)) {
        throw new RangeError("mission route controller options must be positive");
    }
    const obstacleRecoveryAttempts = options.obstacleRecoveryAttempts ?? 0;
    const obstacleRecoveryMs = options.obstacleRecoveryMs ?? 1_000;
    if (!Number.isInteger(obstacleRecoveryAttempts) ||
        obstacleRecoveryAttempts < 0 || obstacleRecoveryAttempts > 4 ||
        !Number.isFinite(obstacleRecoveryMs) || obstacleRecoveryMs <= 0) {
        throw new RangeError("mission route obstacle recovery options are invalid");
    }

    let activeRun = null;
    const controller = {
        get active() { return activeRun !== null; },
        cancel() { if (activeRun) activeRun.canceled = true; },
        async run(routeValue, { signal } = {}) {
            if (activeRun) throw new Error("a mission route is already running");
            const route = parseMissionRoute(routeValue);
            const run = { canceled: false };
            activeRun = run;
            const held = new Set();
            const events = [];
            try {
                let state = routeState(await adapter.observe(), 0);
                for (let segmentIndex = 0;
                    segmentIndex < route.segments.length; ++segmentIndex) {
                    const segment = route.segments[segmentIndex];
                    const began = state.timestampMs;
                    const progressionBefore = {
                        ...state.progression,
                        checkpoint: { ...state.checkpoint },
                    };
                    let closest = distanceTo(segment.targetRegion, state.origin);
                    let lastProgress = began;
                    let lastPulse = Number.NEGATIVE_INFINITY;
                    let died = false;
                    let recoveryCount = 0;
                    events.push({ type: "segment-start", segmentIndex,
                        timestampMs: began, distance: closest });

                    for (;;) {
                        if (run.canceled || signal?.aborted) {
                            throw new MissionRouteError(
                                MISSION_ROUTE_FAILURE.CANCELED,
                                "mission route was canceled", segmentIndex);
                        }
                        state = routeState(await adapter.observe(), segmentIndex);
                        const elapsed = state.timestampMs - began;
                        if (state.health <= 0) {
                            await releaseInput(adapter, held);
                            if (segment.restartPolicy !== "resume") {
                                throw new MissionRouteError(
                                    MISSION_ROUTE_FAILURE.PLAYER_DIED,
                                    "player died before the route segment completed",
                                    segmentIndex);
                            }
                            died = true;
                            if (elapsed >= segment.maxDurationMs) {
                                throw new MissionRouteError(
                                    MISSION_ROUTE_FAILURE.TIMEOUT,
                                    "canonical checkpoint restart timed out",
                                    segmentIndex);
                            }
                            await adapter.wait(tickMs);
                            continue;
                        }
                        if (died) {
                            died = false;
                            closest = distanceTo(segment.targetRegion, state.origin);
                            lastProgress = state.timestampMs;
                            events.push({ type: "checkpoint-restart", segmentIndex,
                                timestampMs: state.timestampMs });
                        }

                        const distance = distanceTo(segment.targetRegion, state.origin);
                        if (segment.divergenceRadius !== undefined &&
                            distance > segment.divergenceRadius) {
                            throw new MissionRouteError(
                                MISSION_ROUTE_FAILURE.DIVERGED,
                                `player diverged ${distance.toFixed(1)} units from target`,
                                segmentIndex);
                        }
                        const progressionMet = progressionChanged(
                            segment.expectedProgression,
                            progressionBefore, {
                                ...state.progression,
                                checkpoint: state.checkpoint,
                            });
                        if (distance <= segment.targetRegion.radius && progressionMet &&
                            elapsed >= (segment.minimumDurationMs ?? 0)) {
                            await releaseInput(adapter, held);
                            events.push({ type: "segment-complete", segmentIndex,
                                timestampMs: state.timestampMs, distance });
                            break;
                        }
                        if (elapsed >= segment.maxDurationMs) {
                            throw new MissionRouteError(
                                MISSION_ROUTE_FAILURE.TIMEOUT,
                                progressionMet
                                    ? "route segment timed out before reaching its target"
                                    : "route segment timed out before expected progression",
                                segmentIndex);
                        }
                        const actions = segment.actions ?? {};
                        if (distance > segment.targetRegion.radius) {
                            if (distance <= closest - minimumProgress) {
                                closest = distance;
                                lastProgress = state.timestampMs;
                            } else if (state.timestampMs - lastProgress >=
                                segment.stuckTimeoutMs) {
                                if (recoveryCount < obstacleRecoveryAttempts) {
                                    await setHeld(adapter, held, "forward", false);
                                    const recoveryKey = recoveryCount % 2 === 0
                                        ? "right" : "left";
                                    await setHeld(adapter, held, recoveryKey, true);
                                    await adapter.wait(obstacleRecoveryMs);
                                    await setHeld(adapter, held, recoveryKey, false);
                                    ++recoveryCount;
                                    lastProgress = state.timestampMs;
                                    closest = distance;
                                    events.push({ type: "obstacle-recovery",
                                        segmentIndex,
                                        timestampMs: state.timestampMs,
                                        attempt: recoveryCount });
                                    continue;
                                }
                                throw new MissionRouteError(
                                    MISSION_ROUTE_FAILURE.STUCK,
                                    "player stopped making route progress",
                                    segmentIndex);
                            }
                        }
                        let facingTarget = false;
                        if (distance > segment.targetRegion.radius ||
                            actions.fire === true) {
                            const aimOrigin = state.aimOrigin ?? state.origin;
                            const deltaX = segment.targetRegion.x - aimOrigin[0];
                            const deltaY = segment.targetRegion.y - aimOrigin[1];
                            const deltaZ = segment.targetRegion.z - aimOrigin[2];
                            const desiredYaw = Math.atan2(deltaY, deltaX) * 180 / Math.PI;
                            const desiredPitch = -Math.atan2(
                                deltaZ, Math.hypot(deltaX, deltaY)) * 180 / Math.PI;
                            const yawError = angleDelta(desiredYaw, state.viewAngles[1]);
                            const pitchError = angleDelta(desiredPitch, state.viewAngles[0]);
                            await adapter.mouse(
                                -clamp(Math.round(yawError * mouseCountsPerDegree),
                                    -maximumMouseDelta, maximumMouseDelta),
                                clamp(Math.round(pitchError * mouseCountsPerDegree),
                                    -maximumMouseDelta, maximumMouseDelta));
                            facingTarget = Math.abs(yawError) < 75;
                        }
                        await setHeld(adapter, held, "forward",
                            distance > segment.targetRegion.radius && facingTarget);
                        await setHeld(adapter, held, "ads", actions.ads === true);
                        await setHeld(adapter, held, "fire", actions.fire === true);
                        if (state.timestampMs - lastPulse >= 1_000) {
                            for (const [action, key] of [["use", "use"], ["jump", "jump"]]) {
                                if (actions[action] === true) {
                                    await adapter.key(key, true);
                                    await adapter.key(key, false);
                                }
                            }
                            lastPulse = state.timestampMs;
                        }
                        await adapter.wait(tickMs);
                    }
                }
                return { schemaVersion: MISSION_ROUTE_SCHEMA_VERSION,
                    map: route.map, validationResult: "pass", events };
            } finally {
                await releaseInput(adapter, held);
                activeRun = null;
            }
        },
    };
    return Object.freeze(controller);
}

function sanitizedObservation(value)
{
    const state = routeState(value, -1);
    return {
        timestampMs: state.timestampMs,
        origin: state.origin.slice(0, 3),
        viewAngles: state.viewAngles.slice(0, 3),
        health: state.health,
        progression: { ...state.progression },
        mission: {
            activeActors: state.mission?.activeActors ?? 0,
            aliveActors: state.mission?.aliveActors ?? 0,
            scriptThreads: state.mission?.scriptThreads ?? 0,
        },
        checkpoint: {
            committed: state.checkpoint?.committed === true,
            saveId: state.checkpoint?.saveId ?? 0,
            checksum: state.checkpoint?.checksum ?? 0,
        },
    };
}

export function createMissionRouteRecorder({ map, radius = 96 } = {})
{
    parseMissionRoute({ schemaVersion: MISSION_ROUTE_SCHEMA_VERSION, map,
        segments: [{ targetRegion: { x: 0, y: 0, z: 0, radius },
            maxDurationMs: 100 }] });
    let startedAt = null;
    let initial = null;
    let latest = null;
    let previousMarker = null;
    let inputStart = 0;
    const observations = [];
    const inputTransitions = [];
    const segments = [];
    return Object.freeze({
        recordObservation(value) {
            const observation = sanitizedObservation(value);
            if (startedAt === null) startedAt = observation.timestampMs;
            observation.timestampMs -= startedAt;
            if (!initial) initial = observation;
            latest = observation;
            const previous = observations.at(-1);
            if (!previous || observation.timestampMs - previous.timestampMs >= 250 ||
                JSON.stringify(observation.progression) !==
                    JSON.stringify(previous.progression) ||
                observation.health <= 0 !== (previous.health <= 0) ||
                observation.checkpoint.checksum !== previous.checkpoint.checksum) {
                observations.push(observation);
            }
            return observation;
        },
        recordInput(event, timestampMs) {
            if (!event || !["key", "mouse-move"].includes(event.type) ||
                !Number.isFinite(timestampMs) || startedAt === null) return false;
            if (event.type === "key" && (!Number.isInteger(event.key) ||
                typeof event.down !== "boolean")) return false;
            if (event.type === "mouse-move" &&
                (![event.dx, event.dy].every(Number.isFinite))) return false;
            inputTransitions.push(event.type === "key"
                ? { timestampMs: timestampMs - startedAt, type: "key",
                    key: event.key, down: event.down }
                : { timestampMs: timestampMs - startedAt, type: "mouse-move",
                    dx: event.dx, dy: event.dy });
            return true;
        },
        markWaypoint(options = {}) {
            if (!latest) throw new Error("record an observation before marking a waypoint");
            const elapsed = latest.timestampMs - (previousMarker?.timestampMs ?? 0);
            const segmentInputs = inputTransitions.slice(inputStart);
            const expectedProgression = {};
            const progressionBaseline = previousMarker ?? initial;
            if (progressionBaseline) {
                if (latest.progression.objectiveHash !==
                    progressionBaseline.progression.objectiveHash) {
                    expectedProgression.objectiveHashChanged = true;
                    expectedProgression.activeObjectives =
                        latest.progression.activeObjectives;
                }
                const doneDelta = latest.progression.doneObjectives -
                    progressionBaseline.progression.doneObjectives;
                if (doneDelta > 0) expectedProgression.doneObjectivesDeltaAtLeast = doneDelta;
                if (latest.progression.missionFlags !==
                    progressionBaseline.progression.missionFlags)
                    expectedProgression.missionFlagsChanged = true;
                if (latest.checkpoint.saveId !== progressionBaseline.checkpoint.saveId ||
                    latest.checkpoint.checksum !== progressionBaseline.checkpoint.checksum)
                    expectedProgression.checkpointChanged = true;
            }
            const pressed = new Set(segmentInputs
                .filter((event) => event.type === "key" && event.down)
                .map((event) => event.key));
            const actions = {
                ...(pressed.has(0xC9) ? { ads: true } : {}),
                ...(pressed.has(0xC8) ? { fire: true } : {}),
                ...(pressed.has(0x20) ? { jump: true } : {}),
                ...(pressed.has(0x66) ? { use: true } : {}),
            };
            const maxDurationMs = Math.max(10_000,
                Math.min(600_000, Math.ceil(elapsed * 3 / 1_000) * 1_000));
            const targetRegion = options.targetRegion ?? {
                x: latest.origin[0], y: latest.origin[1],
                z: latest.origin[2], radius,
            };
            segments.push({
                targetRegion: { ...targetRegion },
                maxDurationMs,
                ...(options.minimumDurationMs === undefined ? {} : {
                    minimumDurationMs: options.minimumDurationMs,
                }),
                stuckTimeoutMs: Math.min(10_000, maxDurationMs),
                restartPolicy: "resume",
                ...(Object.keys(expectedProgression).length === 0
                    ? {} : { expectedProgression }),
                ...(Object.keys(actions).length === 0 ? {} : { actions }),
            });
            previousMarker = latest;
            inputStart = inputTransitions.length;
            return segments.length;
        },
        finish() {
            if (segments.length === 0) throw new Error("mark at least one waypoint");
            return {
                route: parseMissionRoute({
                    schemaVersion: MISSION_ROUTE_SCHEMA_VERSION, map, segments,
                }),
                evidence: {
                    schemaVersion: MISSION_ROUTE_SCHEMA_VERSION,
                    map,
                    observations: structuredClone(observations),
                    inputTransitions: structuredClone(inputTransitions),
                },
            };
        },
    });
}
