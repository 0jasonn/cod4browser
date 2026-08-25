function engineKey(event)
{
    if (/^Key[A-Z]$/.test(event.code)) return event.code.charCodeAt(3) + 32;
    if (/^Digit[0-9]$/.test(event.code)) return event.code.charCodeAt(5);
    if (/^F(?:[1-9]|1[0-5])$/.test(event.code)) return 0xA7 + Number(event.code.slice(1)) - 1;
    return ({
        Tab: 0x09, Enter: 0x0D, Escape: 0x1B, Space: 0x20,
        Backspace: 0x7F, ArrowUp: 0x9A, ArrowDown: 0x9B,
        ArrowLeft: 0x9C, ArrowRight: 0x9D,
        AltLeft: 0x9E, AltRight: 0x9E,
        ControlLeft: 0x9F, ControlRight: 0x9F,
        ShiftLeft: 0xA0, ShiftRight: 0xA0,
        Insert: 0xA1, Delete: 0xA2, PageDown: 0xA3, PageUp: 0xA4,
        Home: 0xA5, End: 0xA6,
    })[event.code] ?? 0;
}

export function createInputController({ canvas, commandInput, engine })
{
    const held = new Set();
    let disposed = false;
    const sendKey = (key, down) => {
        if (!key || disposed) return;
        void engine.input({ type: "key", key, down }).catch(() => {});
    };
    const handleKeyDown = (event) => {
        if (event.target === commandInput) return;
        const key = engineKey(event);
        if (!key) return;
        event.preventDefault();
        if (held.has(key)) return;
        held.add(key);
        sendKey(key, true);
    };
    const handleKeyUp = (event) => {
        const key = engineKey(event);
        if (!held.delete(key)) return;
        event.preventDefault();
        sendKey(key, false);
    };
    const releaseKeys = () => {
        for (const key of held) sendKey(key, false);
        held.clear();
    };
    const handleCanvasClick = () => {
        canvas.focus();
        if (document.pointerLockElement === canvas) return;
        try {
            Promise.resolve(canvas.requestPointerLock({ unadjustedMovement: true }))
                .catch(() => canvas.requestPointerLock());
        } catch {
            canvas.requestPointerLock();
        }
    };
    const handleMouseMove = (event) => {
        if (document.pointerLockElement !== canvas ||
            (!event.movementX && !event.movementY)) return;
        void engine.input({
            type: "mouse-move",
            x: Math.round(canvas.width / 2),
            y: Math.round(canvas.height / 2),
            dx: Math.round(event.movementX),
            dy: Math.round(event.movementY),
        }).catch(() => {});
    };
    const handleCursor = (event) => {
        if (event.detail?.visible === true && document.pointerLockElement === canvas) {
            document.exitPointerLock();
        }
    };

    globalThis.addEventListener("keydown", handleKeyDown);
    globalThis.addEventListener("keyup", handleKeyUp);
    globalThis.addEventListener("blur", releaseKeys);
    canvas.addEventListener("click", handleCanvasClick);
    globalThis.addEventListener("mousemove", handleMouseMove);
    globalThis.addEventListener("kisakcod:cursor", handleCursor);

    return Object.freeze({
        dispose() {
            if (disposed) return;
            releaseKeys();
            disposed = true;
            globalThis.removeEventListener("keydown", handleKeyDown);
            globalThis.removeEventListener("keyup", handleKeyUp);
            globalThis.removeEventListener("blur", releaseKeys);
            canvas.removeEventListener("click", handleCanvasClick);
            globalThis.removeEventListener("mousemove", handleMouseMove);
            globalThis.removeEventListener("kisakcod:cursor", handleCursor);
        },
    });
}
