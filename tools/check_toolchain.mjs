import { readFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = fileURLToPath(new URL("../", import.meta.url));
const readJson = (path) => JSON.parse(readFileSync(join(root, path), "utf8"));
const expected = { ...readJson("package.json").engines, ...readJson("tools/web_toolchain.json") };

export function validateToolVersions(actual, web = false)
{
    for (const name of web ? ["node", "npm", "emscripten", "emsdkCommit", "emscriptenCommit", "cmake", "ninja"] : ["node", "npm"]) {
        if (actual[name] !== expected[name]) {
            throw new Error(`Unsupported ${name}: expected ${expected[name]}, found ${actual[name] ?? "missing"}. ` +
                (name === "node" || name === "npm" ? "Install the versions in package.json and .node-version, then run npm ci."
                    : "Run tools/bootstrap_web_toolchain.ps1; do not substitute a global compiler."));
        }
    }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    try {
        const npmCli = process.env.npm_execpath ?? join(dirname(process.execPath), "node_modules/npm/bin/npm-cli.js");
        const actual = { node: process.versions.node,
            npm: execFileSync(process.execPath, [npmCli, "--version"], { encoding: "utf8" }).trim() };
        const web = process.argv.includes("--web");
        if (web) {
            const sdk = join(root, ".tools/emsdk");
            Object.assign(actual, {
                emscripten: JSON.parse(readFileSync(join(sdk, "upstream/emscripten/emscripten-version.txt"), "utf8")),
                emscriptenCommit: readFileSync(join(sdk, "upstream/emscripten/emscripten-revision.txt"), "utf8").trim(),
                emsdkCommit: execFileSync("git", ["-C", sdk, "rev-parse", "HEAD"], { encoding: "utf8" }).trim(),
                cmake: execFileSync(join(sdk, `cmake/${expected.cmake}_64bit/bin/cmake.exe`), ["--version"], { encoding: "utf8" }).split(/\s/u)[2],
                ninja: execFileSync(join(sdk, `ninja/${expected.ninja}_64bit/ninja.exe`), ["--version"], { encoding: "utf8" }).trim(),
            });
        }
        validateToolVersions(actual, web);
        console.log(`Toolchain verified: ${JSON.stringify(actual)}`);
    } catch (error) {
        console.error(`Toolchain preflight failed: ${error.message}`);
        process.exitCode = 1;
    }
}
