import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { validateToolVersions } from "../../tools/check_toolchain.mjs";

test("preflight rejects missing or unsupported tools using versioned pins", () => {
    const expected = {
        ...JSON.parse(readFileSync(new URL("../../package.json", import.meta.url))).engines,
        ...JSON.parse(readFileSync(new URL("../../tools/web_toolchain.json", import.meta.url))),
    };
    assert.doesNotThrow(() => validateToolVersions(expected, true));
    for (const name of ["node", "npm", "emscripten", "emsdkCommit", "cmake", "ninja"]) {
        for (const value of [undefined, "unsupported"]) {
            assert.throws(() => validateToolVersions({ ...expected, [name]: value }, true),
                new RegExp(`Unsupported ${name}: expected`, "u"));
        }
    }
});
