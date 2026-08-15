import { mkdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { deflateSync } from "node:zlib";
import { REQUIRED_ASSETS } from "../../web/asset_store.mjs";
import { createSyntheticIwd } from "./synthetic_iwd.mjs";

export const SYNTHETIC_LOCALIZATION = [
    "english",
    "",
    "SYNTHETIC_M12_INSTALL_PROFILE",
    '"Freely generated browser test fixture"',
    "",
].join("\n");

export function createSyntheticFastfileHeader()
{
    return Uint8Array.from([
        0x49, 0x57, 0x66, 0x66, 0x75, 0x31, 0x30, 0x30,
        0x05, 0x00, 0x00, 0x00,
        0x78, 0xda,
    ]);
}

function appendU32(bytes, value)
{
    bytes.push(value & 0xff, (value >>> 8) & 0xff,
        (value >>> 16) & 0xff, (value >>> 24) & 0xff);
}

function setU32(bytes, offset, value)
{
    bytes[offset] = value & 0xff;
    bytes[offset + 1] = (value >>> 8) & 0xff;
    bytes[offset + 2] = (value >>> 16) & 0xff;
    bytes[offset + 3] = (value >>> 24) & 0xff;
}

function setU16(bytes, offset, value)
{
    bytes[offset] = value & 0xff;
    bytes[offset + 1] = (value >>> 8) & 0xff;
}

function createSyntheticShaderProgram(vertex)
{
    const bindings = vertex
        ? [["viewProjectionMatrix", 2, 0, 4], ["worldMatrix", 2, 4, 4]]
        : [["colorMapSampler", 3, 0, 1]];
    const version = vertex ? 0xfffe_0101 : 0xffff_0200;
    const table = new Array(28 + bindings.length * 20 + bindings.length * 16).fill(0);
    const appendString = (value) => {
        const offset = table.length;
        table.push(...Buffer.from(value, "ascii"), 0);
        return offset;
    };
    const creator = appendString("web synthetic");
    const target = appendString(vertex ? "vs_1_1" : "ps_2_0");
    const names = bindings.map(([name]) => appendString(name));
    while (table.length % 4 !== 0) table.push(0);
    setU32(table, 0, 28);
    setU32(table, 4, creator);
    setU32(table, 8, version);
    setU32(table, 12, bindings.length);
    setU32(table, 16, 28);
    setU32(table, 24, target);
    const typeBase = 28 + bindings.length * 20;
    bindings.forEach(([, registerSet, registerIndex, registerCount], index) => {
        const info = 28 + index * 20;
        setU32(table, info, names[index]);
        setU16(table, info + 4, registerSet);
        setU16(table, info + 6, registerIndex);
        setU16(table, info + 8, registerCount);
        setU32(table, info + 12, typeBase + index * 16);
        const type = typeBase + index * 16;
        setU16(table, type, vertex ? 3 : 4);
        setU16(table, type + 2, vertex ? 3 : 12);
        setU16(table, type + 4, vertex ? 4 : 1);
        setU16(table, type + 6, vertex ? 4 : 1);
        setU16(table, type + 8, 1);
    });
    const words = [version,
        ((1 + table.length / 4) << 16) | 0xfffe, 0x4241_5443];
    for (let offset = 0; offset < table.length; offset += 4) {
        words.push((table[offset] | table[offset + 1] << 8 |
            table[offset + 2] << 16 | table[offset + 3] << 24) >>> 0);
    }
    const instruction = (opcode, operands) => {
        words.push((vertex ? opcode : opcode | operands << 24) >>> 0);
        words.push(...new Array(operands).fill(0));
    };
    if (vertex) {
        instruction(0x51, 5);
        for (let index = 0; index < 3; ++index) instruction(0x1f, 2);
        instruction(0x04, 4);
        for (let index = 0; index < 8; ++index) instruction(0x09, 3);
        for (let index = 0; index < 2; ++index) instruction(0x01, 2);
    } else {
        for (let index = 0; index < 3; ++index) instruction(0x1f, 2);
        instruction(0x42, 3);
        instruction(0x05, 3);
        instruction(0x01, 2);
    }
    words.push(0x0000_ffff);
    return words;
}

// Freely generated structural fixture for the M17 paired-shader path.
export function createSyntheticRetailCensusFastfile()
{
    const inflated = [];
    appendU32(inflated, 1_378_265);
    appendU32(inflated, 950_499);
    for (const size of [498_816, 0, 0, 0, 407_412, 0, 0, 4_224, 480]) {
        appendU32(inflated, size);
    }
    appendU32(inflated, 3);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 5);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0xffff_ffff);
    for (const text of ["end", "tag_origin"]) {
        inflated.push(...Buffer.from(text, "ascii"), 0);
    }
    const assets = [
        [5, 0xffff_ffff],
        [5, 0xffff_ffff],
        [4, 0xffff_fffe],
        [22, 0x4000_0011],
        [32, 0],
    ];
    for (const [type, reference] of assets) {
        appendU32(inflated, type);
        appendU32(inflated, reference);
    }

    const techniqueSet = new Array(148).fill(0);
    setU32(techniqueSet, 0, 0xffff_ffff);
    setU32(techniqueSet, 12 + 4 * 4, 0xffff_ffff);
    inflated.push(...techniqueSet, ...Buffer.from("web/synthetic_techset", "ascii"), 0);

    appendU32(inflated, 0xffff_ffff);
    inflated.push(0, 0, 1, 0);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0xffff_ffff);
    inflated.push(1, 1, 1, 0);
    appendU32(inflated, 0xffff_ffff);

    const vertexDeclaration = new Array(100).fill(0);
    vertexDeclaration[0] = 3;
    vertexDeclaration[6] = 1;
    vertexDeclaration[7] = 2;
    vertexDeclaration[8] = 2;
    vertexDeclaration[9] = 4;
    inflated.push(...vertexDeclaration);

    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0);
    appendU32(inflated, 0xffff_ffff);
    const vertexProgram = createSyntheticShaderProgram(true);
    appendU32(inflated, vertexProgram.length);
    inflated.push(...Buffer.from("web_synthetic_vs", "ascii"), 0);
    for (const word of vertexProgram) {
        appendU32(inflated, word);
    }
    appendU32(inflated, 0x4000_00ed);
    appendU32(inflated, 0);
    appendU32(inflated, 0xffff_ffff);
    const pixelProgram = createSyntheticShaderProgram(false);
    appendU32(inflated, pixelProgram.length);
    for (const word of pixelProgram) appendU32(inflated, word);
    for (const word of [0x0004_0003, 0x0400_003c, 0x0000_0003,
        0x0400_004c, 0x0000_0002, 0xa0ab_1041]) {
        appendU32(inflated, word);
    }
    inflated.push(...Buffer.from("web_synthetic2d", "ascii"), 0);
    inflated.push(...new Array(64).fill(0xa5));
    const compressed = deflateSync(Uint8Array.from(inflated), { level: 9 });
    return Uint8Array.from([
        0x49, 0x57, 0x66, 0x66, 0x75, 0x31, 0x30, 0x30,
        0x05, 0x00, 0x00, 0x00,
        ...compressed,
    ]);
}

export async function createInstallDirectory(
    testInfo,
    name,
    {
        localization = SYNTHETIC_LOCALIZATION,
        primaryIwd = createSyntheticIwd(),
        overrides = new Map(),
        omit = [],
    } = {},
)
{
    const directory = testInfo.outputPath(name);
    const omitted = new Set(omit);
    for (const requirement of REQUIRED_ASSETS) {
        if (omitted.has(requirement.path)) {
            continue;
        }
        const target = path.join(directory, ...requirement.path.split("/"));
        await mkdir(path.dirname(target), { recursive: true });
        let contents = overrides.get(requirement.path);
        if (contents === undefined) {
            if (requirement.kind === "localization") {
                contents = localization;
            } else if (requirement.path === "main/iw_00.iwd") {
                contents = primaryIwd;
            } else if (requirement.kind === "iwd") {
                contents = createSyntheticIwd();
            } else if (requirement.path === "zone/english/code_post_gfx.ff") {
                contents = createSyntheticRetailCensusFastfile();
            } else {
                contents = createSyntheticFastfileHeader();
            }
        }
        await writeFile(
            target,
            contents,
            requirement.kind === "localization" ? "utf8" : undefined,
        );
    }
    return directory;
}
