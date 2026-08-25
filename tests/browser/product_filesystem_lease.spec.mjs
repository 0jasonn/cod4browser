import { expect, test } from "@playwright/test";

test.skip(process.env.KISAK_WEB_PRODUCT_TEST !== "1", "Runs only against the production site.");

async function installLeaseHarness(page)
{
    await page.goto("/");
    await page.evaluate(async () => {
        const { createEngineWorkerHost } = await import("/product_engine_worker_host.mjs");
        const { ENGINE_PROTOCOL_VERSION } = await import("/product_protocol.mjs");

        class WorkerDouble extends EventTarget
        {
            constructor(mode)
            {
                super();
                this.mode = mode;
                this.flushFailed = false;
                this.terminated = false;
            }

            send(data, delay = 0)
            {
                setTimeout(() => this.dispatchEvent(new MessageEvent("message", { data })), delay);
            }

            reply(message, result, error = null, delay = 0)
            {
                this.send({
                    protocolVersion: ENGINE_PROTOCOL_VERSION,
                    type: "reply",
                    id: message.id,
                    operation: message.type,
                    result,
                    error,
                }, delay);
            }

            postMessage(message)
            {
                if (message.type === "init") {
                    this.send({ protocolVersion: ENGINE_PROTOCOL_VERSION, type: "ready" });
                } else if (message.type === "mountAssets" && this.mode === "stall") {
                    // Deliberately leave ownership uncertain until the host terminates us.
                } else if (message.type === "mountAssets" && this.mode === "late") {
                    this.reply(message, { mounted: true }, null, 250);
                } else if (message.type === "flushAndUnmount" &&
                    this.mode === "retryable" && !this.flushFailed) {
                    this.flushFailed = true;
                    this.reply(message, null, {
                        code: "OPERATION_FAILED",
                        operation: message.type,
                        message: "injected retryable flush failure",
                        recoverable: true,
                    });
                } else {
                    this.reply(message, message.type === "mountAssets"
                        ? { mounted: true }
                        : { mounted: false });
                }
            }

            terminate() { this.terminated = true; }
        }

        globalThis.createLeaseHost = async (mode = "normal", timeout = 100) => {
            const worker = new WorkerDouble(mode);
            const lifecycle = [];
            const states = [];
            const canvas = document.createElement("canvas");
            const host = createEngineWorkerHost(canvas, {
                workerFactory: () => worker,
                audioDriverFactory: () => ({ attachGestureResume() {}, dispose() {} }),
                managePageLifecycle: false,
                filesystemStallTimeoutMs: timeout,
                filesystemAbsoluteTimeoutMs: timeout * 5,
                onFilesystemLifecycleEvent: (event) => lifecycle.push(event),
                onFilesystemState: (state) => states.push(state),
            });
            await host.ready;
            globalThis.leaseHarness = { host, worker, lifecycle, states };
        };
        globalThis.mountLeaseHost = async () => {
            try {
                await globalThis.leaseHarness.host.mountAssets({ importId: "lease-test", files: [] });
                return { ok: true };
            } catch (error) {
                return { ok: false, code: error.code, message: error.message };
            }
        };
        globalThis.flushLeaseHost = async () => {
            try {
                await globalThis.leaseHarness.host.flushAndUnmount();
                return { ok: true };
            } catch (error) {
                return { ok: false, code: error.code, message: error.message };
            }
        };
        globalThis.disposeLeaseHost = () => globalThis.leaseHarness?.host.dispose();
    });
}

test("two tabs hand off the writable profile after a clean unmount @product", async ({ context }) => {
    const first = await context.newPage();
    const second = await context.newPage();
    await Promise.all([installLeaseHarness(first), installLeaseHarness(second)]);
    await Promise.all([
        first.evaluate(() => globalThis.createLeaseHost()),
        second.evaluate(() => globalThis.createLeaseHost()),
    ]);

    expect(await first.evaluate(() => globalThis.mountLeaseHost())).toEqual({ ok: true });
    expect(await second.evaluate(() => globalThis.mountLeaseHost())).toMatchObject({
        ok: false, code: "HOME_WRITER_CONFLICT",
    });
    expect(await first.evaluate(() => globalThis.flushLeaseHost())).toEqual({ ok: true });
    expect(await second.evaluate(() => globalThis.mountLeaseHost())).toEqual({ ok: true });

    await Promise.all([
        first.evaluate(() => globalThis.disposeLeaseHost()),
        second.evaluate(() => globalThis.disposeLeaseHost()),
    ]);
});

test("retryable flush failure keeps the writer lease until retry succeeds @product", async ({ context }) => {
    const first = await context.newPage();
    const second = await context.newPage();
    await Promise.all([installLeaseHarness(first), installLeaseHarness(second)]);
    await first.evaluate(() => globalThis.createLeaseHost("retryable"));
    await second.evaluate(() => globalThis.createLeaseHost());

    expect(await first.evaluate(() => globalThis.mountLeaseHost())).toEqual({ ok: true });
    expect(await first.evaluate(() => globalThis.flushLeaseHost())).toMatchObject({
        ok: false, code: "OPERATION_FAILED",
    });
    expect(await second.evaluate(() => globalThis.mountLeaseHost())).toMatchObject({
        ok: false, code: "HOME_WRITER_CONFLICT",
    });
    expect(await first.evaluate(() => globalThis.flushLeaseHost())).toEqual({ ok: true });
    expect(await second.evaluate(() => globalThis.mountLeaseHost())).toEqual({ ok: true });

    await Promise.all([
        first.evaluate(() => globalThis.disposeLeaseHost()),
        second.evaluate(() => globalThis.disposeLeaseHost()),
    ]);
});

test("timeout terminates the old worker before releasing its writer lease @product", async ({ context }) => {
    const first = await context.newPage();
    const second = await context.newPage();
    await Promise.all([installLeaseHarness(first), installLeaseHarness(second)]);
    await first.evaluate(() => globalThis.createLeaseHost("stall", 1_000));
    await second.evaluate(() => globalThis.createLeaseHost());

    const timedOutMount = first.evaluate(() => globalThis.mountLeaseHost());
    await expect.poll(() => first.evaluate(() =>
        globalThis.leaseHarness.states.includes("mounting"))).toBe(true);
    expect(await second.evaluate(() => globalThis.mountLeaseHost())).toMatchObject({
        ok: false, code: "HOME_WRITER_CONFLICT",
    });
    expect(await timedOutMount).toMatchObject({ ok: false, code: "REQUEST_TIMEOUT" });
    expect(await first.evaluate(() => globalThis.leaseHarness.lifecycle)).toEqual([
        "writerLeaseAcquired",
        "workerTerminationStarted",
        "workerTerminated",
        "writerLeaseReleased",
    ]);
    expect(await second.evaluate(() => globalThis.mountLeaseHost())).toEqual({ ok: true });

    await Promise.all([
        first.evaluate(() => globalThis.disposeLeaseHost()),
        second.evaluate(() => globalThis.disposeLeaseHost()),
    ]);
});

test("late reply from a terminated worker cannot change the new tab session @product", async ({ context }) => {
    const first = await context.newPage();
    const second = await context.newPage();
    await Promise.all([installLeaseHarness(first), installLeaseHarness(second)]);
    await first.evaluate(() => globalThis.createLeaseHost("late", 50));
    await second.evaluate(() => globalThis.createLeaseHost());

    expect(await first.evaluate(() => globalThis.mountLeaseHost())).toMatchObject({
        ok: false, code: "REQUEST_TIMEOUT",
    });
    expect(await second.evaluate(() => globalThis.mountLeaseHost())).toEqual({ ok: true });
    await first.waitForTimeout(300);
    expect(await first.evaluate(() => globalThis.leaseHarness.host.filesystemState))
        .toBe("terminated");
    expect(await second.evaluate(() => globalThis.leaseHarness.host.filesystemState))
        .toBe("mounted");

    await Promise.all([
        first.evaluate(() => globalThis.disposeLeaseHost()),
        second.evaluate(() => globalThis.disposeLeaseHost()),
    ]);
});
