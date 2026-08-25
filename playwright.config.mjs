import { defineConfig, devices } from "@playwright/test";

const browserChannel = process.env.KISAK_BROWSER_CHANNEL;
const siteDirectory = process.env.KISAK_WEB_SITE ?? "build/web-diagnostics/site-diagnostics";
const serverPort = Number(process.env.KISAK_WEB_TEST_PORT ?? 8000);
const baseURL = `http://127.0.0.1:${serverPort}`;

export default defineConfig({
    testDir: "./tests/browser",
    fullyParallel: false,
    forbidOnly: Boolean(process.env.CI),
    retries: process.env.CI ? 2 : 0,
    workers: Number(process.env.KISAK_PLAYWRIGHT_WORKERS ?? 2),
    reporter: "list",
    timeout: 30_000,
    expect: {
        timeout: 10_000,
    },
    use: {
        baseURL,
        trace: "retain-on-failure",
        viewport: { width: 1440, height: 1000 },
    },
    webServer: {
        command: `python tools/serve_web.py --directory ${siteDirectory} --port ${serverPort}`,
        url: baseURL,
        reuseExistingServer: !process.env.CI,
        timeout: 15_000,
    },
    projects: [
        {
            name: "chromium",
            use: {
                ...devices["Desktop Chrome"],
                ...(browserChannel ? { channel: browserChannel } : {}),
            },
        },
    ],
});
