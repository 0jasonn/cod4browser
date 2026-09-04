import assert from "node:assert/strict";
import test from "node:test";

import { mountWorkerFilesystem } from "../../web/worker_sync_filesystem.mjs";

function filesystem({ mountError = null, checkpointError = null, cleanupError = null } = {})
{
    const calls = [];
    return {
        calls,
        async mount() {
            calls.push("mount");
            if (mountError) throw mountError;
            return { fileCount: 1 };
        },
        observeReadProgress() { return () => {}; },
        async checkpoint() {
            calls.push("checkpoint");
            if (checkpointError) throw checkpointError;
        },
        async flushAndUnmount() {
            calls.push("cleanup");
            if (cleanupError) throw cleanupError;
        },
    };
}

test("mount failure before ownership is classified clean after cleanup", async () => {
    const fs = filesystem({ mountError: new Error("filesystem mount failed") });
    await assert.rejects(
        mountWorkerFilesystem(fs, {}, () => fs.calls.push("runtime")),
        (error) => error.code === "MOUNT_FAILED_CLEAN",
    );
    assert.deepEqual(fs.calls, ["mount", "cleanup"]);
});

test("partial filesystem mount cleanup can prove a clean failure", async () => {
    const fs = filesystem({ mountError: new Error("partial filesystem mount failed") });
    await assert.rejects(
        mountWorkerFilesystem(fs, {}, () => fs.calls.push("runtime")),
        (error) => error.code === "MOUNT_FAILED_CLEAN",
    );
    assert.equal(fs.calls.at(-1), "cleanup");
});

test("canonical runtime mount failure leaves ownership unknown", async () => {
    const fs = filesystem();
    await assert.rejects(mountWorkerFilesystem(fs, {}, () => {
        fs.calls.push("runtime");
        throw new Error("runtime mount failed");
    }), (error) => error.code === "FILESYSTEM_OWNERSHIP_UNKNOWN");
    assert.deepEqual(fs.calls, ["mount", "runtime", "cleanup"]);
});

test("checkpoint failure after runtime mount leaves ownership unknown", async () => {
    const fs = filesystem({ checkpointError: new Error("checkpoint failed") });
    await assert.rejects(
        mountWorkerFilesystem(fs, {}, () => fs.calls.push("runtime")),
        (error) => error.code === "FILESYSTEM_OWNERSHIP_UNKNOWN",
    );
    assert.deepEqual(fs.calls, ["mount", "runtime", "checkpoint", "cleanup"]);
});

test("cleanup failure after a partial mount is explicit", async () => {
    const fs = filesystem({
        mountError: new Error("mount failed"),
        cleanupError: new Error("cleanup failed"),
    });
    await assert.rejects(
        mountWorkerFilesystem(fs, {}, () => fs.calls.push("runtime")),
        (error) => error.code === "MOUNT_CLEANUP_FAILED" &&
            error.message.includes("cleanup failed"),
    );
    assert.deepEqual(fs.calls, ["mount", "cleanup"]);
});
