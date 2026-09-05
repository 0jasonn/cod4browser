const KEY_CODES = /** @type {Readonly<Record<string, number>>} */ (Object.freeze({
    Tab: 0x09, Enter: 0x0D, Escape: 0x1B, Space: 0x20,
    Backspace: 0x7F, CapsLock: 0x97, Pause: 0x99,
    ArrowUp: 0x9A, ArrowDown: 0x9B, ArrowLeft: 0x9C, ArrowRight: 0x9D,
    AltLeft: 0x9E, AltRight: 0x9E,
    ControlLeft: 0x9F, ControlRight: 0x9F,
    ShiftLeft: 0xA0, ShiftRight: 0xA0,
    Insert: 0xA1, Delete: 0xA2, PageDown: 0xA3, PageUp: 0xA4,
    Home: 0xA5, End: 0xA6,
    Minus: 0x2D, Equal: 0x3D, BracketLeft: 0x5B, BracketRight: 0x5D,
    Backslash: 0x5C, Semicolon: 0x3B, Quote: 0x27, Backquote: 0x60,
    Comma: 0x2C, Period: 0x2E, Slash: 0x2F,
    Numpad7: 0xB6, Numpad8: 0xB7, Numpad9: 0xB8,
    Numpad4: 0xB9, Numpad5: 0xBA, Numpad6: 0xBB,
    Numpad1: 0xBC, Numpad2: 0xBD, Numpad3: 0xBE,
    NumpadEnter: 0xBF, Numpad0: 0xC0, NumpadDecimal: 0xC1,
    NumpadDivide: 0xC2, NumpadSubtract: 0xC3, NumpadAdd: 0xC4,
    NumLock: 0xC5, NumpadMultiply: 0xC6, NumpadEqual: 0xC7,
}));

/** @param {KeyboardEvent} event */
function browserKeyToEngineKey(event)
{
    if (/^Key[A-Z]$/.test(event.code)) return event.code.charCodeAt(3) + 32;
    if (/^Digit[0-9]$/.test(event.code)) return event.code.charCodeAt(5);
    if (/^F(?:[1-9]|1[0-5])$/.test(event.code)) {
        return 0xA7 + Number(event.code.slice(1)) - 1;
    }
    return KEY_CODES[event.code] ?? 0;
}

/** @param {number} button */
const mouseButtonKey = (button) => [0xC8, 0xCA, 0xC9, 0xCB, 0xCC][button] ?? 0;

// Kisak's native ANSI fields store bytes, not UTF-8. Match the Western
// installation code page; never truncate an unsupported Unicode character.
const westernCharacters = new TextDecoder("windows-1252")
    .decode(Uint8Array.from({ length: 256 }, (_, index) => index));
/** @param {string} text */
export function browserTextToEngineCharacters(text)
{
    return Array.from(text.normalize("NFC"), (character) =>
        westernCharacters.indexOf(character)).filter((byte) => byte >= 32 && byte !== 127);
}

/**
 * @typedef {{type: "key", key: number, down: boolean} |
 *   {type: "char", character: number} |
 *   {type: "clipboard", characters: number[]} |
 *   {type: "mouse-move", x: number, y: number, dx: number, dy: number}} EngineInput
 * @param {{canvas: HTMLCanvasElement, commandInput?: HTMLElement | null,
 *   textInput?: HTMLTextAreaElement | null,
 *   sendInput: (event: EngineInput) => unknown,
 *   onState?: (state: Record<string, boolean>) => void,
 *   onFailure?: (error: unknown) => void,
 *   requestFrame?: (callback: FrameRequestCallback) => number,
 *   cancelFrame?: (handle: number) => void}} options
 */
export function createInputControllerCore({
    canvas,
    commandInput = null,
    textInput = null,
    sendInput,
    onState = () => {},
    onFailure = () => {},
    requestFrame = (callback) => requestAnimationFrame(callback),
    cancelFrame = (handle) => cancelAnimationFrame(handle),
})
{
    const heldKeys = new Set();
    const heldMouseButtons = new Set();
    let absoluteMouse = false;
    let disposed = false;
    let deliveryFailed = false;
    let pointerWasLocked = document.pointerLockElement === canvas;
    let programmaticUnlock = false;
    let lastForwardedEscape = Number.NEGATIVE_INFINITY;
    let lastSyntheticEscape = Number.NEGATIVE_INFINITY;
    /** @type {number | null} */
    let movementFrame = null;
    let pendingMovementX = 0;
    let pendingMovementY = 0;
    const escapeDeduplicationMilliseconds = 100;

    const clearRelativeMovement = () => {
        if (movementFrame !== null) cancelFrame(movementFrame);
        movementFrame = null;
        pendingMovementX = 0;
        pendingMovementY = 0;
    };
    /** @param {unknown} error */
    const failDelivery = (error) => {
        if (deliveryFailed || disposed) return;
        deliveryFailed = true;
        clearRelativeMovement();
        heldKeys.clear();
        heldMouseButtons.clear();
        onFailure(error);
    };
    /** @param {EngineInput} event */
    const send = (event) => {
        if (disposed || deliveryFailed) return;
        try {
            Promise.resolve(sendInput(event)).catch(failDelivery);
        } catch (error) {
            failDelivery(error);
        }
    };
    /** @param {number} key @param {boolean} down */
    const sendKey = (key, down) => {
        if (key) send({ type: "key", key, down });
    };
    const inputActive = () => document.pointerLockElement === canvas ||
        document.activeElement === canvas ||
        (textInput !== null && document.activeElement === textInput);
    const releaseHeldInput = () => {
        for (const key of heldKeys) sendKey(key, false);
        for (const key of heldMouseButtons) sendKey(key, false);
        heldKeys.clear();
        heldMouseButtons.clear();
    };
    const releasePointerLock = () => {
        if (document.pointerLockElement !== canvas) return;
        programmaticUnlock = true;
        document.exitPointerLock?.();
    };
    const flushRelativeMovement = () => {
        if (!pendingMovementX && !pendingMovementY) return;
        send({
            type: "mouse-move",
            x: Math.max(0, Math.round((canvas.width - 1) * 0.5)),
            y: Math.max(0, Math.round((canvas.height - 1) * 0.5)),
            dx: Math.round(pendingMovementX),
            dy: Math.round(pendingMovementY),
        });
        pendingMovementX = 0;
        pendingMovementY = 0;
    };

    /** @param {KeyboardEvent} event */
    const handleKeyDown = (event) => {
        if (event.target === commandInput || !inputActive()) return;
        // Let the browser finish dead-key/IME composition before committing
        // text. Intermediate composition keys are not game commands.
        if (event.isComposing || event.key === "Dead" || event.key === "Process") return;
        const key = browserKeyToEngineKey(event);
        const altGraph = event.getModifierState?.("AltGraph") === true;
        const shiftPasteShortcut = event.shiftKey &&
            (key === 0xA1 || key === 0xC0);
        const pasteShortcut = shiftPasteShortcut || event.key?.toLowerCase() === "v" &&
            (event.metaKey || (event.ctrlKey && !altGraph));
        let character = 0;
        if (!event.metaKey && (!event.altKey || altGraph)) {
            if (event.key === "Backspace") character = 8;
            else if (event.ctrlKey && !altGraph) {
                // Native field shortcuts: start, clear, end. Clipboard access
                // remains a separate browser permission/gesture boundary.
                character = ({ a: 1, c: 3, e: 5 })[event.key?.toLowerCase()] ?? 0;
            } else if (event.key && Array.from(event.key).length === 1) {
                character = browserTextToEngineCharacters(event.key)[0] ?? 0;
            }
        }
        if (!key && !character) return;
        // The trusted paste event owns synchronous clipboard access. Let the
        // browser dispatch it, then route its bytes through Field_Paste.
        // Shift+Insert would invoke Field_Paste from CL_KeyEvent too early, so
        // defer that shortcut entirely to the following paste event.
        if (shiftPasteShortcut) return;
        if (!pasteShortcut) event.preventDefault();
        if (key === 0x1B &&
            performance.now() - lastSyntheticEscape < escapeDeduplicationMilliseconds) return;
        if (key) heldKeys.add(key);
        if (key === 0x1B) lastForwardedEscape = performance.now();
        // CL_KeyEvent owns repeat suppression for gameplay and repeat admission
        // for console/menu navigation. Char events repeat independently.
        sendKey(key, true);
        if (character) send({ type: "char", character });
    };
    /** @param {ClipboardEvent} event */
    const handlePaste = (event) => {
        if (event.target === commandInput || !inputActive()) return;
        const text = event.clipboardData?.getData("text/plain");
        if (typeof text !== "string") return;
        const firstLine = text.split(/[\n\r\b]/, 1)[0];
        const characters = browserTextToEngineCharacters(firstLine).slice(0, 4095);
        if (!characters.length) return;
        event.preventDefault();
        send({ type: "clipboard", characters });
        send({ type: "char", character: 22 });
    };
    /** @param {CompositionEvent} event */
    const handleCompositionEnd = (event) => {
        if (!textInput || event.target !== textInput || !inputActive()) return;
        for (const character of browserTextToEngineCharacters(event.data)) {
            send({ type: "char", character });
        }
        if (textInput) textInput.value = "";
    };
    const clearTextInput = () => { if (textInput) textInput.value = ""; };
    /** @param {KeyboardEvent} event */
    const handleKeyUp = (event) => {
        const key = browserKeyToEngineKey(event);
        if (!key || !heldKeys.has(key)) return;
        event.preventDefault();
        heldKeys.delete(key);
        if (key === 0x1B &&
            performance.now() - lastSyntheticEscape < escapeDeduplicationMilliseconds) return;
        sendKey(key, false);
    };
    /** @param {MouseEvent} event */
    const handleMouseDown = (event) => {
        if (textInput) textInput.focus({ preventScroll: true });
        else canvas.focus();
        event.preventDefault();
        if (document.pointerLockElement === canvas) pointerWasLocked = true;
        const key = mouseButtonKey(event.button);
        if (key && !heldMouseButtons.has(key)) {
            heldMouseButtons.add(key);
            sendKey(key, true);
        }
        if (absoluteMouse || document.pointerLockElement === canvas ||
            !canvas.requestPointerLock) return;
        try {
            Promise.resolve(canvas.requestPointerLock({ unadjustedMovement: true }))
                .catch(() => canvas.requestPointerLock())
                .catch(() => {});
        } catch (_) {
            try {
                Promise.resolve(canvas.requestPointerLock()).catch(() => {});
            } catch (_) {
                // Pointer lock requires a supported, trusted user gesture.
            }
        }
    };
    /** @param {MouseEvent} event */
    const handleMouseUp = (event) => {
        const key = mouseButtonKey(event.button);
        if (!key || !heldMouseButtons.delete(key)) return;
        event.preventDefault();
        sendKey(key, false);
    };
    /** @param {MouseEvent} event */
    const handleMouseMove = (event) => {
        const pointerLocked = document.pointerLockElement === canvas;
        if (!pointerLocked && (!absoluteMouse || event.target !== canvas)) return;
        if (pointerLocked && !event.movementX && !event.movementY) return;
        if (pointerLocked) {
            pendingMovementX += event.movementX;
            pendingMovementY += event.movementY;
            if (movementFrame === null) {
                movementFrame = requestFrame(() => {
                    movementFrame = null;
                    flushRelativeMovement();
                });
            }
            return;
        }
        const bounds = canvas.getBoundingClientRect();
        if (canvas.width <= 0 || canvas.height <= 0 ||
            bounds.width <= 0 || bounds.height <= 0) return;
        const x = Math.round((event.clientX - bounds.left) * canvas.width /
            bounds.width);
        const y = Math.round((event.clientY - bounds.top) * canvas.height /
            bounds.height);
        send({
            type: "mouse-move",
            x: Math.max(0, Math.min(canvas.width - 1, x)),
            y: Math.max(0, Math.min(canvas.height - 1, y)),
            dx: 0,
            dy: 0,
        });
    };
    /** @param {WheelEvent} event */
    const handleWheel = (event) => {
        if (!inputActive() || event.deltaY === 0) return;
        event.preventDefault();
        const key = event.deltaY < 0 ? 0xCE : 0xCD;
        sendKey(key, true);
        sendKey(key, false);
    };
    const handlePointerLockChange = () => {
        const pointerLocked = document.pointerLockElement === canvas;
        onState({ pointerLocked });
        if (pointerWasLocked && !pointerLocked) {
            flushRelativeMovement();
            const intendedUnlock = programmaticUnlock;
            programmaticUnlock = false;
            if (!intendedUnlock && document.hasFocus() &&
                performance.now() - lastForwardedEscape >=
                    escapeDeduplicationMilliseconds) {
                lastSyntheticEscape = performance.now();
                sendKey(0x1B, true);
                sendKey(0x1B, false);
            }
            releaseHeldInput();
        }
        pointerWasLocked = pointerLocked;
    };
    /** @param {Event} event */
    const handleCursor = (event) => {
        const cursorVisible = /** @type {CustomEvent} */ (event).detail?.visible === true;
        onState({ cursorVisible });
        if (cursorVisible) releasePointerLock();
    };
    /** @param {Event} event */
    const handleMouseMode = (event) => {
        absoluteMouse = /** @type {CustomEvent} */ (event).detail?.absolute === true;
        onState({ absoluteMouse });
        if (absoluteMouse) releasePointerLock();
    };
    const handleVisibility = () => {
        if (document.visibilityState !== "hidden") return;
        clearRelativeMovement();
        releaseHeldInput();
    };
    const handleBlur = () => {
        clearRelativeMovement();
        releaseHeldInput();
    };
    /** @param {Event} event */
    function preventDefault(event) { event.preventDefault(); }

    globalThis.addEventListener("keydown", handleKeyDown);
    globalThis.addEventListener("keyup", handleKeyUp);
    globalThis.addEventListener("compositionend", handleCompositionEnd);
    globalThis.addEventListener("paste", handlePaste);
    globalThis.addEventListener("mouseup", handleMouseUp);
    globalThis.addEventListener("mousemove", handleMouseMove);
    globalThis.addEventListener("blur", handleBlur);
    globalThis.addEventListener("kisakcod:cursor", handleCursor);
    globalThis.addEventListener("kisakcod:mouse-mode", handleMouseMode);
    document.addEventListener("pointerlockchange", handlePointerLockChange);
    document.addEventListener("visibilitychange", handleVisibility);
    canvas.addEventListener("mousedown", handleMouseDown);
    canvas.addEventListener("wheel", handleWheel, { passive: false });
    canvas.addEventListener("auxclick", preventDefault);
    canvas.addEventListener("contextmenu", preventDefault);
    textInput?.addEventListener("input", clearTextInput);

    return Object.freeze({
        release() {
            clearRelativeMovement();
            releaseHeldInput();
            releasePointerLock();
        },
        dispose() {
            if (disposed) return;
            clearRelativeMovement();
            releaseHeldInput();
            releasePointerLock();
            disposed = true;
            globalThis.removeEventListener("keydown", handleKeyDown);
            globalThis.removeEventListener("keyup", handleKeyUp);
            globalThis.removeEventListener("compositionend", handleCompositionEnd);
            globalThis.removeEventListener("paste", handlePaste);
            globalThis.removeEventListener("mouseup", handleMouseUp);
            globalThis.removeEventListener("mousemove", handleMouseMove);
            globalThis.removeEventListener("blur", handleBlur);
            globalThis.removeEventListener("kisakcod:cursor", handleCursor);
            globalThis.removeEventListener("kisakcod:mouse-mode", handleMouseMode);
            document.removeEventListener("pointerlockchange", handlePointerLockChange);
            document.removeEventListener("visibilitychange", handleVisibility);
            canvas.removeEventListener("mousedown", handleMouseDown);
            canvas.removeEventListener("wheel", handleWheel);
            canvas.removeEventListener("auxclick", preventDefault);
            canvas.removeEventListener("contextmenu", preventDefault);
            textInput?.removeEventListener("input", clearTextInput);
        },
    });
}
