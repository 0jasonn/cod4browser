import assert from "node:assert/strict";
import test from "node:test";

import {
    createMissionRouteController,
    createMissionRouteRecorder,
    MISSION_ROUTE_FAILURE,
    parseMissionRoute,
} from "../../web/diagnostic_mission_route.mjs";

const progression = (objectiveHash = 1, activeObjectives = 1,
    doneObjectives = 0, missionFlags = 0) => ({
    objectiveHash, activeObjectives, doneObjectives, missionFlags,
});

const state = (timestampMs, origin, overrides = {}) => ({
    timestampMs,
    origin,
    viewAngles: [0, 0, 0],
    health: 100,
    progression: progression(),
    ...overrides,
});

const route = (segment = {}) => ({
    schemaVersion: 1,
    map: "village_assault",
    segments: [{
        targetRegion: { x: 100, y: 0, z: 0, radius: 10 },
        maxDurationMs: 2_000,
        stuckTimeoutMs: 1_000,
        ...segment,
    }],
});

function fakeAdapter(states, onWait = () => {})
{
    let index = 0;
    const inputs = [];
    return {
        inputs,
        async observe() {
            const value = states[Math.min(index, states.length - 1)];
            ++index;
            return structuredClone(value);
        },
        async key(key, down) { inputs.push({ type: "key", key, down }); },
        async mouse(dx, dy) { inputs.push({ type: "mouse", dx, dy }); },
        async wait(milliseconds) { await onWait(milliseconds); },
    };
}

test("mission route parser accepts the versioned sanitized schema", () => {
    const parsed = parseMissionRoute(route({
        expectedProgression: { doneObjectivesDeltaAtLeast: 1 },
        actions: { ads: true, fire: true, use: true },
        minimumDurationMs: 500,
        restartPolicy: "resume",
    }));
    assert.equal(parsed.schemaVersion, 1);
    assert.equal(parsed.map, "village_assault");
    assert.equal(parsed.segments[0].actions.fire, true);
    assert.equal(parsed.segments[0].actions.ads, true);
    assert.equal(parsed.segments[0].minimumDurationMs, 500);
});

test("mission route completes a waypoint through normal input", async () => {
    const adapter = fakeAdapter([
        state(0, [0, 0, 0]),
        state(100, [50, 0, 0]),
        state(200, [95, 0, 0]),
    ]);
    const result = await createMissionRouteController(adapter).run(route());
    assert.equal(result.validationResult, "pass");
    assert.equal(result.events.at(-1).type, "segment-complete");
    assert(adapter.inputs.some((input) =>
        input.type === "key" && input.key === "forward" && input.down));
});

test("mission route uses COD yaw direction for a positive-y waypoint", async () => {
    const adapter = fakeAdapter([
        state(0, [0, 0, 0]),
        state(100, [0, 0, 0]),
        state(200, [0, 95, 0]),
    ]);
    await createMissionRouteController(adapter).run(route({
        targetRegion: { x: 0, y: 100, z: 0, radius: 10 },
    }));
    assert(adapter.inputs.some((input) =>
        input.type === "mouse" && input.dx < 0));
});

test("mission route can hold position and fire toward a target", async () => {
    const adapter = fakeAdapter([
        state(0, [0, 0, 0]),
        state(1_000, [0, 0, 0]),
        state(2_000, [0, 0, 0]),
    ]);
    const result = await createMissionRouteController(adapter).run(route({
        targetRegion: { x: 100, y: 0, z: 0, radius: 128 },
        minimumDurationMs: 2_000,
        actions: { fire: true },
    }));
    assert.equal(result.events.at(-1).timestampMs, 2_000);
    assert(adapter.inputs.some((input) =>
        input.type === "key" && input.key === "fire" && input.down));
    assert.equal(adapter.inputs.some((input) =>
        input.type === "key" && input.key === "forward" && input.down), false);
});

test("mission route aims fire from the canonical view origin", async () => {
    const adapter = fakeAdapter([
        state(0, [0, 0, 0], { aimOrigin: [0, 0, 60] }),
        state(1_000, [0, 0, 0], { aimOrigin: [0, 0, 60] }),
        state(2_000, [0, 0, 0], { aimOrigin: [0, 0, 60] }),
    ]);
    await createMissionRouteController(adapter).run(route({
        targetRegion: { x: 100, y: 0, z: 60, radius: 128 },
        minimumDurationMs: 2_000,
        actions: { fire: true },
    }));
    assert(adapter.inputs.some((input) =>
        input.type === "mouse" && input.dy === 0));
});

test("mission route reports a stuck player", async () => {
    const adapter = fakeAdapter([
        state(0, [0, 0, 0]), state(0, [0, 0, 0]),
        state(500, [0, 0, 0]), state(1_000, [0, 0, 0]),
    ]);
    await assert.rejects(createMissionRouteController(adapter).run(route()),
        (error) => error.code === MISSION_ROUTE_FAILURE.STUCK);
});

test("mission route can recover from an obstacle through normal strafe input",
    async () => {
        const adapter = fakeAdapter([
            state(0, [0, 0, 0]), state(0, [0, 0, 0]),
            state(1_000, [0, 0, 0]), state(2_000, [20, 0, 0]),
            state(3_000, [20, 0, 0]), state(3_100, [95, 0, 0]),
        ]);
        const result = await createMissionRouteController(adapter, {
            obstacleRecoveryAttempts: 2,
        }).run(route({ maxDurationMs: 5_000 }));
        assert(result.events.some((event) =>
            event.type === "obstacle-recovery" && event.attempt === 1));
        assert(result.events.some((event) =>
            event.type === "obstacle-recovery" && event.attempt === 2));
        assert(adapter.inputs.some((input) =>
            input.type === "key" && input.key === "right" && input.down));
        assert(adapter.inputs.some((input) =>
            input.type === "key" && input.key === "left" && input.down));
    });

test("mission route reports a segment timeout", async () => {
    const adapter = fakeAdapter([
        state(0, [0, 0, 0]), state(0, [0, 0, 0]),
        state(1_000, [1, 0, 0]),
    ]);
    await assert.rejects(createMissionRouteController(adapter).run(route({
        maxDurationMs: 1_000,
    })), (error) => error.code === MISSION_ROUTE_FAILURE.TIMEOUT);
});

test("mission route reports an unexpected player death", async () => {
    const adapter = fakeAdapter([
        state(0, [0, 0, 0]), state(100, [0, 0, 0], { health: 0 }),
    ]);
    await assert.rejects(createMissionRouteController(adapter).run(route()),
        (error) => error.code === MISSION_ROUTE_FAILURE.PLAYER_DIED);
});

test("mission route waits for its objective expectation", async () => {
    const adapter = fakeAdapter([
        state(0, [100, 0, 0]),
        state(100, [100, 0, 0]),
        state(200, [100, 0, 0], { progression: progression(2) }),
    ]);
    const result = await createMissionRouteController(adapter).run(route({
        expectedProgression: { objectiveHashChanged: true },
    }));
    assert.equal(result.events.at(-1).timestampMs, 200);
});

test("mission route waits for a canonical checkpoint change", async () => {
    const adapter = fakeAdapter([
        state(0, [100, 0, 0], {
            checkpoint: { committed: false, saveId: 0, checksum: 0 },
        }),
        state(100, [100, 0, 0], {
            checkpoint: { committed: false, saveId: 0, checksum: 0 },
        }),
        state(200, [100, 0, 0], {
            checkpoint: { committed: true, saveId: 1, checksum: 123 },
        }),
    ]);
    const result = await createMissionRouteController(adapter).run(route({
        expectedProgression: { checkpointChanged: true },
    }));
    assert.equal(result.events.at(-1).timestampMs, 200);
});

test("mission route resumes only after a canonical restart observation", async () => {
    const adapter = fakeAdapter([
        state(0, [0, 0, 0]),
        state(100, [0, 0, 0], { health: 0 }),
        state(200, [100, 0, 0]),
    ]);
    const result = await createMissionRouteController(adapter).run(route({
        restartPolicy: "resume",
    }));
    assert(result.events.some((event) => event.type === "checkpoint-restart"));
    assert.equal(result.events.at(-1).type, "segment-complete");
});

test("mission route rejects invalid and mutation-shaped routes", () => {
    assert.throws(() => parseMissionRoute({ ...route(), schemaVersion: 2 }),
        (error) => error.code === MISSION_ROUTE_FAILURE.INVALID_ROUTE);
    assert.throws(() => parseMissionRoute(route({ teleport: [100, 0, 0] })),
        (error) => error.code === MISSION_ROUTE_FAILURE.INVALID_ROUTE);
    assert.throws(() => parseMissionRoute(route({ minimumDurationMs: 2_001 })),
        (error) => error.code === MISSION_ROUTE_FAILURE.INVALID_ROUTE);
});

test("mission route cancellation releases held input", async () => {
    let controller;
    const adapter = fakeAdapter([
        state(0, [0, 0, 0]), state(100, [1, 0, 0]),
    ], () => controller.cancel());
    controller = createMissionRouteController(adapter);
    await assert.rejects(controller.run(route()),
        (error) => error.code === MISSION_ROUTE_FAILURE.CANCELED);
    assert(adapter.inputs.some((input) =>
        input.type === "key" && input.key === "forward" && !input.down));
});

test("mission route controller exposes no gameplay-state mutation API", () => {
    const adapter = fakeAdapter([state(0, [100, 0, 0])]);
    const controller = createMissionRouteController(adapter);
    assert.deepEqual(Object.keys(controller).sort(), ["active", "cancel", "run"]);
    for (const name of ["command", "setObjective", "setState", "teleport"])
        assert.equal(name in controller, false);
});

test("mission route authoring records sparse sanitized waypoints", () => {
    const recorder = createMissionRouteRecorder({ map: "village_assault" });
    recorder.recordObservation({
        ...state(1_000, [0, 0, 0]),
        mission: { activeActors: 2, aliveActors: 2, scriptThreads: 3 },
        checkpoint: { committed: false, saveId: 0, checksum: 0 },
    });
    recorder.recordInput({ type: "key", key: 0x66, down: true }, 1_100);
    recorder.recordObservation({
        ...state(6_000, [100, 0, 0], {
            progression: progression(2, 1, 1, 0),
        }),
        mission: { activeActors: 2, aliveActors: 2, scriptThreads: 3 },
        checkpoint: { committed: true, saveId: 1, checksum: 123 },
    });
    recorder.markWaypoint();
    const authored = recorder.finish();
    assert.equal(authored.route.segments.length, 1);
    assert.equal(authored.route.segments[0].actions.use, true);
    assert.equal(authored.route.segments[0].expectedProgression.objectiveHashChanged,
        true);
    assert.equal(authored.route.segments[0].expectedProgression.checkpointChanged,
        true);
    assert.equal(authored.evidence.observations[0].timestampMs, 0);
    assert.equal(JSON.stringify(authored).includes("setObjective"), false);
});
