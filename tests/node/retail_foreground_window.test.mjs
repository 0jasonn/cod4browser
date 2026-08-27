import assert from "node:assert/strict";
import test from "node:test";

import {
    INVALID_BACKGROUND_THROTTLED,
    summarizeForegroundSamples,
} from "../browser/retail_foreground_window.mjs";

test("foreground retail window remains valid while visible and focused", () => {
    assert.deepEqual(summarizeForegroundSamples([
        { observedMs: 0, visibilityState: "visible", pageFocused: true },
        { observedMs: 1_000, visibilityState: "visible", pageFocused: true },
    ]), {
        pageVisibilityState: "visible",
        pageFocused: true,
        finalPageVisibilityState: "visible",
        finalPageFocused: true,
        backgroundTransitions: 0,
        foregroundStateTransitions: [],
        performanceWindowValid: true,
        performanceInvalidReason: null,
    });
});

test("foreground retail window rejects any background transition", () => {
    const summary = summarizeForegroundSamples([
        { observedMs: 0, visibilityState: "visible", pageFocused: true },
        { observedMs: 1_000, visibilityState: "hidden", pageFocused: false },
        { observedMs: 2_000, visibilityState: "visible", pageFocused: true },
    ]);
    assert.equal(summary.performanceWindowValid, false);
    assert.equal(summary.performanceInvalidReason, INVALID_BACKGROUND_THROTTLED);
    assert.equal(summary.backgroundTransitions, 2);
    assert.deepEqual(summary.foregroundStateTransitions[0].to, {
        visibilityState: "hidden",
        pageFocused: false,
    });
});
