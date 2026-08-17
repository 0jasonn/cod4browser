export async function installGate2OracleRequest(page)
{
    await page.addInitScript(() => {
        globalThis.addEventListener("kisakcod:assets", (event) => {
            if (event.detail.state === "ready") {
                globalThis.__KISAKCOD_WEB__.startGate2Oracle();
            }
        });
    });
}

export async function requestGate2Oracle(page)
{
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.startGate2Oracle());
}
