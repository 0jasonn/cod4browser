globalThis.postMessage({ syncAccessHandle: typeof FileSystemFileHandle === "function" &&
    typeof FileSystemFileHandle.prototype.createSyncAccessHandle === "function" });
