export const INVALID_BACKGROUND_THROTTLED = "INVALID_BACKGROUND_THROTTLED";

export function summarizeForegroundSamples(samples)
{
    if (!Array.isArray(samples) || samples.length === 0) {
        throw new TypeError("foreground samples must be a non-empty array");
    }

    const transitions = [];
    for (let index = 1; index < samples.length; ++index) {
        const previous = samples[index - 1];
        const current = samples[index];
        if (previous.visibilityState !== current.visibilityState ||
            previous.pageFocused !== current.pageFocused) {
            transitions.push({
                observedMs: current.observedMs,
                from: {
                    visibilityState: previous.visibilityState,
                    pageFocused: previous.pageFocused,
                },
                to: {
                    visibilityState: current.visibilityState,
                    pageFocused: current.pageFocused,
                },
            });
        }
    }

    const first = samples[0];
    const last = samples.at(-1);
    const performanceWindowValid = samples.every((sample) =>
        sample.visibilityState === "visible" && sample.pageFocused === true);
    return {
        pageVisibilityState: first.visibilityState,
        pageFocused: first.pageFocused,
        finalPageVisibilityState: last.visibilityState,
        finalPageFocused: last.pageFocused,
        backgroundTransitions: transitions.length,
        foregroundStateTransitions: transitions,
        performanceWindowValid,
        performanceInvalidReason: performanceWindowValid
            ? null : INVALID_BACKGROUND_THROTTLED,
    };
}
