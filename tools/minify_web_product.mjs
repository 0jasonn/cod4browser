import { readdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { minify } from "terser";

const directory = process.argv[2];
if (!directory) throw new Error("Usage: node tools/minify_web_product.mjs SITE_DIRECTORY");
for (const name of await readdir(directory)) {
    // Emscripten already optimizes these modules; preserve its exact export
    // syntax for the independent production application-export check.
    if (!name.endsWith(".mjs") || ["kisakcod.mjs", "reverb_dsp.mjs"].includes(name)) continue;
    const path = join(directory, name);
    const source = await readFile(path, "utf8");
    const { code } = await minify(source, {
        module: true,
        compress: false,
        mangle: true,
        format: { comments: /^!/ },
    });
    if (!code) throw new Error(`Minification produced no code for ${name}`);
    await writeFile(path, code + "\n");
}
