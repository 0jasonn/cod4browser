import { chromium, expect, test } from "@playwright/test";
import { readFile } from "node:fs/promises";

test("OPFS rename and restore recover after browser restart on the same origin and profile", async ({ baseURL }, testInfo) => {
    test.setTimeout(120_000);
    const workerSource = await readFile(new URL("./fixtures/home_recovery_worker.mjs", import.meta.url), "utf8");
    for (const operation of ["interrupt-journal", "interrupt-write", "fail-write",
        "restore-journal", "restore-write", "restore-fail-write"]) {
        const profile = testInfo.outputPath(`profile-${operation}`);
        async function open()
        {
            const context = await chromium.launchPersistentContext(profile, { headless: true });
            await context.route("**/home_recovery_worker.mjs", (route) => route.fulfill({
                contentType: "text/javascript", body: workerSource,
                headers: { "Cross-Origin-Embedder-Policy": "require-corp", "Cross-Origin-Resource-Policy": "same-origin" },
            }));
            const page = await context.newPage();
            await page.goto(baseURL);
            // Startup garbage collection retires unregistered import directories.
            // Create the fixture only after that lifecycle has finished.
            await expect(page.locator(".asset-control")).toHaveAttribute("data-asset-state", "empty");
            return { context, page };
        }
        async function request(page, message)
        {
            return page.evaluate((data) => new Promise((resolve, reject) => {
                const worker = new Worker("/home_recovery_worker.mjs", { type: "module" });
                worker.onmessage = ({ data: reply }) => resolve(reply);
                worker.onerror = (event) => reject(new Error(event.message));
                worker.postMessage(data);
            }), message);
        }
        let session = await open();
        try {
            const interrupted = await request(session.page, { operation, shutdown: true });
            expect(interrupted.error, operation).toBeUndefined();
            if (operation.endsWith("fail-write")) {
                expect(interrupted).toEqual({ failure: "QuotaExceededError", durable: "old complete save" });
            } else expect(interrupted.phase).toBe(operation.endsWith("journal") ? "journal-published" : "staged-partial-write");
        } finally { await session.context.close(); }
        // Reuse this exact directory and origin. A new empty profile is not a
        // recovery test. Repeat restart to prove journal retirement is stable.
        for (let restart = 0; restart < 2; ++restart) {
            session = await open();
            try {
                expect(await request(session.page, { operation: "recover", restore: operation.startsWith("restore-") })).toEqual({
                    recovered: "new complete save", temporary: null,
                });
            } finally { await session.context.close(); }
        }
    }
});
