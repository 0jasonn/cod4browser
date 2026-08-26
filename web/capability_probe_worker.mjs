globalThis.onmessage = ({ data }) => {
    const canvas = data?.canvas;
    const offscreenCanvas = typeof OffscreenCanvas === "function" &&
        canvas instanceof OffscreenCanvas;
    const result = {
        offscreenCanvas,
        syncAccessHandle: typeof FileSystemFileHandle === "function" &&
            typeof FileSystemFileHandle.prototype.createSyncAccessHandle === "function",
        canvas,
    };
    globalThis.postMessage(result, offscreenCanvas ? [canvas] : []);
};
