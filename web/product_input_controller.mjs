import { createInputControllerCore } from "./input_controller_core.mjs";

export function createInputController({ canvas, commandInput, engine })
{
    return createInputControllerCore({
        canvas,
        commandInput,
        sendInput: (event) => engine.input(event),
    });
}
