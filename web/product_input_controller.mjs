import { createInputControllerCore } from "./input_controller_core.mjs";

/**
 * @param {{canvas: HTMLCanvasElement, commandInput: HTMLElement,
 *   engine: {input: (event: import("./input_controller_core.mjs").EngineInput) => unknown},
 *   onFailure?: (error: unknown) => void}} options
 */
export function createInputController({ canvas, commandInput, engine, onFailure })
{
    return createInputControllerCore({
        canvas,
        commandInput,
        sendInput: (event) => engine.input(event),
        onFailure,
    });
}
