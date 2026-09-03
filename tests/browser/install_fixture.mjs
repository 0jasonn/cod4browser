import { mkdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { deflateSync } from "node:zlib";
import { getRequiredAssets } from "../../web/asset_profile.mjs";
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

// Freely generated admission fixture only; zero payload is not a decodable movie.
export function createSyntheticCinematicHeader()
{
    const bytes = Buffer.alloc(64);
    bytes.write("BIKi");
    for (const [offset, value] of [[4, 56], [8, 1], [12, 16], [16, 1],
        [20, 16], [24, 16], [28, 30], [32, 1], [44, 49]])
        bytes.writeUInt32LE(value, offset);
    return bytes;
}

// Freely generated fixture for the canonical runtime-prefix XFile envelope. It keeps
// the first 44 inflated bytes deterministic and can append arbitrary bytes for
// refill/failure coverage without borrowing from retail data.
export function createSyntheticCanonicalXFile({
    size = 4096,
    externalSize = 0,
    blockSizes = [1024, 0, 0, 0, 1024, 0, 0, 0, 0],
    trailingBytes = [],
} = {})
{
    if (blockSizes.length !== 9) throw new Error("XFile requires nine block sizes");
    const inflated = [];
    appendU32(inflated, size);
    appendU32(inflated, externalSize);
    for (const blockSize of blockSizes) appendU32(inflated, blockSize);
    inflated.push(...trailingBytes);
    const compressed = deflateSync(Uint8Array.from(inflated), { level: 9 });
    return Uint8Array.from([
        0x49, 0x57, 0x66, 0x66, 0x75, 0x31, 0x30, 0x30,
        0x05, 0x00, 0x00, 0x00,
        ...compressed,
    ]);
}

export function createSyntheticGeneratedPrefixFastfile({
    scriptStrings = [],
    rawFiles = [],
    scriptStringCount = scriptStrings.length,
    assetCount = rawFiles.length,
    assetType = 31,
    assetPointer = 0xffff_fffe,
    blockSizes = [4096, 0, 0, 0, 4096, 0, 0, 0, 0],
    compressionLevel = 9,
} = {})
{
    const inflated = [];
    appendU32(inflated, 8192);
    appendU32(inflated, 0);
    for (const blockSize of blockSizes) appendU32(inflated, blockSize);
    appendU32(inflated, scriptStringCount);
    appendU32(inflated, scriptStrings.length ? 0xffff_ffff : 0);
    appendU32(inflated, assetCount);
    appendU32(inflated, rawFiles.length ? 0xffff_ffff : 0);
    for (const _identity of scriptStrings) appendU32(inflated, 0xffff_ffff);
    for (const identity of scriptStrings) {
        inflated.push(...Buffer.from(identity, "utf8"), 0);
    }
    for (const _rawFile of rawFiles) {
        appendU32(inflated, assetType);
        appendU32(inflated, assetPointer);
    }
    for (const rawFile of rawFiles) {
        const payload = Buffer.from(rawFile.contents ?? "", "utf8");
        appendU32(inflated, 0xffff_ffff);
        appendU32(inflated, payload.length);
        appendU32(inflated, 1);
        inflated.push(...Buffer.from(rawFile.name, "utf8"), 0);
        inflated.push(...payload, 0);
    }
    const compressed = deflateSync(Uint8Array.from(inflated), {
        level: compressionLevel,
    });
    return Uint8Array.from([
        ...Buffer.from("IWffu100", "ascii"), 5, 0, 0, 0, ...compressed,
    ]);
}

export function createSyntheticCanonicalRefillXFile()
{
    const inflated = [];
    appendU32(inflated, 4096);
    appendU32(inflated, 0);
    for (const blockSize of [1024, 0, 0, 0, 1024, 0, 0, 0, 0]) {
        appendU32(inflated, blockSize);
    }
    appendU32(inflated, 0); // ScriptStringList count
    appendU32(inflated, 0); // ScriptStringList strings
    appendU32(inflated, 0); // XAsset count
    appendU32(inflated, 0); // XAsset array

    // Valid zlib/deflate with enough empty stored blocks to place the first
    // output beyond the native 256 KiB input half-buffer.
    const deflate = [0x78, 0x01];
    for (let index = 0; index < 52_430; ++index) {
        deflate.push(0x00, 0x00, 0x00, 0xff, 0xff);
    }
    const length = inflated.length;
    deflate.push(0x01, length & 0xff, length >>> 8,
        (~length) & 0xff, ((~length) >>> 8) & 0xff, ...inflated);
    let a = 1;
    let b = 0;
    for (const byte of inflated) {
        a = (a + byte) % 65_521;
        b = (b + a) % 65_521;
    }
    const adler = ((b << 16) | a) >>> 0;
    deflate.push(adler >>> 24, (adler >>> 16) & 0xff,
        (adler >>> 8) & 0xff, adler & 0xff);
    return Uint8Array.from([
        ...Buffer.from("IWffu100", "ascii"), 5, 0, 0, 0, ...deflate,
    ]);
}

// Freely generated minimal prerequisite zone. The empty channel file allows
// canonical sound initialization without sound assets in platform tests.
export function createSyntheticEmptyPrerequisiteFastfile()
{
    return createSyntheticGeneratedPrefixFastfile({ rawFiles: [
        { name: "tests/empty_common.txt", contents: "test" },
        { name: "soundaliases/channels.def", contents: "# No sounds in this fixture.\n" },
    ] });
}

function appendU32(bytes, value)
{
    bytes.push(value & 0xff, (value >>> 8) & 0xff,
        (value >>> 16) & 0xff, (value >>> 24) & 0xff);
}

function appendU16(bytes, value)
{
    bytes.push(value & 0xff, (value >>> 8) & 0xff);
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

function setF32(bytes, offset, value)
{
    const buffer = new ArrayBuffer(4);
    new DataView(buffer).setFloat32(0, value, true);
    const encoded = new Uint8Array(buffer);
    for (let index = 0; index < encoded.length; index += 1) {
        bytes[offset + index] = encoded[index];
    }
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

function appendWorldMaterialTechnique(inflated, name, invalidVertexProgram = false)
{
    appendU32(inflated, 0xffff_ffff);
    appendU16(inflated, 0);
    appendU16(inflated, 1);

    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0xffff_ffff);
    inflated.push(0, 0, 1, 0);
    appendU32(inflated, 0xffff_ffff);

    const declaration = new Array(100).fill(0);
    declaration[0] = 1;
    inflated.push(...declaration);

    const vertexProgram = createSyntheticShaderProgram(true);
    if (invalidVertexProgram) vertexProgram[0] = 0;
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, vertexProgram.length);
    inflated.push(...Buffer.from(`${name}_vs`, "ascii"), 0);
    for (const word of vertexProgram) appendU32(inflated, word);

    const pixelProgram = createSyntheticShaderProgram(false);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, pixelProgram.length);
    inflated.push(...Buffer.from(`${name}_ps`, "ascii"), 0);
    for (const word of pixelProgram) appendU32(inflated, word);

    appendU32(inflated, 2);
    appendU32(inflated, 0);
    inflated.push(...Buffer.from(name, "ascii"), 0);
}

// Freely generated structural fixture for the M20 material/image path.
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
        [4, 0xffff_ffff],
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

    const materialTechniqueSet = new Array(148).fill(0);
    setU32(materialTechniqueSet, 0, 0xffff_ffff);
    setU32(materialTechniqueSet, 12 + 4 * 4, 0xffff_ffff);
    inflated.push(...materialTechniqueSet, ...Buffer.from("web/material_techset", "ascii"), 0);
    appendU32(inflated, 0x4000_0385);
    appendU16(inflated, 0);
    appendU16(inflated, 1);
    appendU32(inflated, 0x4000_0079);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0xffff_ffff);
    inflated.push(1, 1, 1, 0);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0x4000_00ed);
    appendU32(inflated, 0);
    appendU32(inflated, 0xffff_ffff);
    appendU16(inflated, vertexProgram.length);
    appendU16(inflated, 1);
    for (const word of vertexProgram) appendU32(inflated, word);
    appendU32(inflated, 0x4000_00ed);
    appendU32(inflated, 0);
    appendU32(inflated, 0xffff_ffff);
    appendU16(inflated, pixelProgram.length);
    appendU16(inflated, 1);
    for (const word of pixelProgram) appendU32(inflated, word);
    for (const word of [0x0004_0003, 0x0400_003c, 0x0000_0003,
        0x0400_004c, 0x0000_0002, 0xa0ab_1041]) {
        appendU32(inflated, word);
    }

    const material = new Array(80).fill(0);
    setU32(material, 0, 0xffff_ffff);
    material[5] = 43;
    material[6] = 1;
    material[7] = 1;
    material[58] = 1;
    material[60] = 1;
    material[62] = 3;
    setU32(material, 64, 0x4000_0029);
    setU32(material, 68, 0xffff_ffff);
    setU32(material, 76, 0xffff_ffff);
    inflated.push(...material, ...Buffer.from("web_cursor", "ascii"), 0);
    appendU32(inflated, 0xa0ab_1041);
    inflated.push("c".charCodeAt(0), "p".charCodeAt(0), 0xe2, 0);
    appendU32(inflated, 0xffff_ffff);

    const image = new Array(36).fill(0);
    setU32(image, 0, 3);
    setU32(image, 4, 0xffff_fffe);
    image[10] = 1;
    setU16(image, 24, 4);
    setU16(image, 26, 4);
    setU16(image, 28, 1);
    image[30] = 3;
    setU32(image, 32, 0xffff_ffff);
    inflated.push(...image, ...Buffer.from("synthetic_engine_asset", "ascii"), 0);
    inflated.push(1, 2);
    appendU16(inflated, 4);
    appendU16(inflated, 4);
    appendU16(inflated, 1);
    appendU32(inflated, 0x3154_5844);
    appendU32(inflated, 0);
    inflated.push(...new Array(8).fill(0));
    const compressed = deflateSync(Uint8Array.from(inflated), { level: 9 });
    return Uint8Array.from([
        0x49, 0x57, 0x66, 0x66, 0x75, 0x31, 0x30, 0x30,
        0x05, 0x00, 0x00, 0x00,
        ...compressed,
    ]);
}

// Freely generated fixture for the M21 table inventory through the M33 second
// XModel surface prefix. It contains no retail bytes.
export function createSyntheticWorldInventoryFastfile({
    includeWorld = true,
    invalidTechniqueSet = false,
    invalidSecondTechniqueSet = false,
    secondTechniqueDependency = false,
    invalidXModelBounds = false,
    unsupportedXModelBoneNames = false,
    invalidXModelBoneString = false,
    invalidXSurfaceLayout = false,
    invalidXSurfaceCollision = false,
    invalidXModelMaterialAlias = false,
    invalidXModelCollisionBounds = false,
    invalidXModelBoneInfo = false,
    xModelPhysPreset = false,
    sharedXModelPhysPreset = false,
    invalidXModelPhysPresetValues = false,
    invalidXModelPhysPresetSoundAlias = false,
    invalidPostXModelTechniqueSet = false,
    postXModelTechniqueDependency = false,
    completePostXModelTechniqueDependencies = false,
    invalidSecondPostXModelTechnique = false,
    invalidLaterPostXModelTechniqueSet = false,
    laterPostXModelTechniqueDependency = false,
    invalidSecondXModelBounds = false,
    unsupportedSecondXModelBoneNames = false,
    invalidSecondXSurfaceLayout = false,
    invalidSecondXModelMaterialAlias = false,
    invalidSecondXModelImageAlias = false,
    externalColorMap = true,
    colorMapName = "synthetic_engine_asset",
    secondExternalColorMap = false,
    secondColorMapName = "synthetic_engine_asset_second",
} = {})
{
    const inflated = [];
    appendU32(inflated, 2_000_000);
    appendU32(inflated, 1_000_000);
    for (const size of [4096, 0, 0, 0,
        completePostXModelTechniqueDependencies ? 16_384 : 4096,
        0, 0, 4096, 4096]) {
        appendU32(inflated, size);
    }
    appendU32(inflated, 1);
    appendU32(inflated, 0xffff_ffff);
    const assets = [
        [5, 0xffff_ffff],
        [5, 0xffff_ffff],
        [3, 0xffff_ffff],
        [xModelPhysPreset ? 16 : 5, 0xffff_ffff],
        [5, 0xffff_ffff],
        [3, 0xffff_ffff],
        [completePostXModelTechniqueDependencies
            ? 5 : includeWorld ? 16 : 32,
        completePostXModelTechniqueDependencies || includeWorld
            ? 0xffff_ffff : 0],
    ];
    if (completePostXModelTechniqueDependencies) {
        assets.push([includeWorld ? 16 : 32, includeWorld ? 0xffff_ffff : 0]);
    }
    appendU32(inflated, assets.length);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0xffff_ffff);
    inflated.push(...Buffer.from("tag_origin", "ascii"), 0);
    for (const [type, reference] of assets) {
        appendU32(inflated, type);
        appendU32(inflated, reference);
    }
    const firstTechniqueSet = new Array(148).fill(0);
    setU32(firstTechniqueSet, 0, 0xffff_ffff);
    if (invalidTechniqueSet) firstTechniqueSet[5] = 1;
    inflated.push(
        ...firstTechniqueSet,
        ...Buffer.from(",web/mc_l_sm_r0c0s0", "ascii"),
        0,
    );
    const secondTechniqueSet = new Array(148).fill(0);
    setU32(secondTechniqueSet, 0, 0xffff_ffff);
    if (invalidSecondTechniqueSet) secondTechniqueSet[5] = 1;
    if (secondTechniqueDependency) {
        setU32(secondTechniqueSet, 12 + 4 * 4, 0xffff_ffff);
    }
    inflated.push(
        ...secondTechniqueSet,
        ...Buffer.from(",web/mc_l_sm_r0c0s1", "ascii"),
        0,
    );
    const xmodel = new Array(220).fill(0);
    setU32(xmodel, 0, 0xffff_ffff);
    xmodel[4] = 1;
    xmodel[5] = 1;
    xmodel[6] = 6;
    setU32(xmodel, 8, unsupportedXModelBoneNames ? 0x4000_0001 : 0xffff_ffff);
    setU32(xmodel, 24, 0xffff_ffff);
    setU32(xmodel, 28, 0xffff_ffff);
    setU32(xmodel, 32, 0xffff_ffff);
    setU32(xmodel, 36, 0xffff_ffff);
    setF32(xmodel, 40, 800);
    // The first LOD contains the two material-bearing surfaces. The remaining
    // serialized surfaces are later-LOD census evidence only.
    setU16(xmodel, 44, 2);
    setU32(xmodel, 48, 0x8000_0000);
    setU32(xmodel, 152, 0xffff_ffff);
    setU32(xmodel, 156, 1);
    setU32(xmodel, 164, 0xffff_ffff);
    setF32(xmodel, 168, 10);
    setF32(xmodel, 172, invalidXModelBounds ? 2 : -1);
    setF32(xmodel, 176, -2);
    setF32(xmodel, 180, -3);
    setF32(xmodel, 184, 1);
    setF32(xmodel, 188, 2);
    setF32(xmodel, 192, 3);
    setU16(xmodel, 196, 1);
    setU16(xmodel, 198, 0);
    setU32(xmodel, 204, 100);
    if (xModelPhysPreset) {
        setU32(xmodel, 212,
            sharedXModelPhysPreset ? 0xffff_fffe : 0xffff_ffff);
    }
    inflated.push(...xmodel, ...Buffer.from("web/xmodel_wall", "ascii"), 0);
    appendU16(inflated, invalidXModelBoneString ? 1 : 0);
    inflated.push(0);
    const baseMat = new Array(32).fill(0);
    setF32(baseMat, 12, 1);
    setF32(baseMat, 28, 1);
    inflated.push(...baseMat);
    for (let index = 0; index < 6; ++index) {
        const surface = new Array(56).fill(0);
        surface[0] = index === 0 ? 1 : 0;
        const vertexCount = index === 0 ? 4 : 3;
        const triangleCount = index === 0 ? 2 : 1;
        setU16(surface, 2, vertexCount);
        setU16(surface, 4, triangleCount);
        setU16(surface, 8, index === 0 ? 0 : index + 1);
        setU16(surface, 10, index === 0 ? 0 : 4 + (index - 1) * 3);
        setU32(surface, 12,
            invalidXSurfaceLayout && index === 0 ? 0 : 0xffff_ffff);
        setU32(surface, 28, 0xffff_ffff);
        setU32(surface, 32, 1);
        setU32(surface, 36, 0xffff_ffff);
        setU32(surface, 40, 0x8000_0000);
        inflated.push(...surface);
    }
    for (let index = 0; index < 6; ++index) {
        if (index === 0) {
            const packedVertices = [
                [-2, -1, 0, 0x0000, 0x0000],
                [-2,  1, 0, 0x0000, 0x3c00],
                [ 2,  1, 0, 0x3c00, 0x3c00],
                [ 2, -1, 0, 0x3c00, 0x0000],
            ];
            for (const [x, y, z, u, v] of packedVertices) {
                const vertex = new Array(32).fill(0);
                setF32(vertex, 0, x);
                setF32(vertex, 4, y);
                setF32(vertex, 8, z);
                setF32(vertex, 12, 1);
                setU32(vertex, 16, 0xffff_ffff);
                setU32(vertex, 20, (u << 16) | v);
                setU32(vertex, 24, 0x7f7f_ffff);
                setU32(vertex, 28, 0x7f7f_ffff);
                inflated.push(...vertex);
            }
        } else if (index === 1) {
            const packedVertices = [
                [-0.6, -0.5, 0, 0x0000, 0x0000],
                [ 0.6, -0.5, 0, 0x3c00, 0x0000],
                [ 0.0,  0.7, 0, 0x3800, 0x3c00],
            ];
            for (const [x, y, z, u, v] of packedVertices) {
                const vertex = new Array(32).fill(0);
                setF32(vertex, 0, x);
                setF32(vertex, 4, y);
                setF32(vertex, 8, z);
                setF32(vertex, 12, 1);
                setU32(vertex, 16, 0xffff_ffff);
                setU32(vertex, 20, (u << 16) | v);
                setU32(vertex, 24, 0x7f7f_ffff);
                setU32(vertex, 28, 0x7f7f_ffff);
                inflated.push(...vertex);
            }
        } else {
            for (let byte = 0; byte < 3 * 32; ++byte) {
                inflated.push((byte + index) & 0xff);
            }
        }
        appendU16(inflated, 0);
        appendU16(inflated, index === 0 ? 4 : 3);
        appendU16(inflated, 0);
        appendU16(inflated, index === 0 ? 2 : 1);
        appendU32(inflated, index === 0 ? 0xffff_ffff : 0);
        if (index === 0) {
            for (let axis = 0; axis < 3; ++axis) appendU32(inflated, 0);
            const collisionTree = new Array(12).fill(0);
            setF32(collisionTree, 0, invalidXSurfaceCollision ? 0 : 1);
            setF32(collisionTree, 4, 1);
            setF32(collisionTree, 8, 1);
            inflated.push(...collisionTree);
            appendU32(inflated, 1);
            appendU32(inflated, 0xffff_ffff);
            appendU32(inflated, 1);
            appendU32(inflated, 0xffff_ffff);
            for (let byte = 0; byte < 16; ++byte) inflated.push(0x40 + byte);
            appendU16(inflated, 0);
        }
        appendU16(inflated, 0);
        appendU16(inflated, 1);
        appendU16(inflated, 2);
        if (index === 0) {
            appendU16(inflated, 2);
            appendU16(inflated, 3);
            appendU16(inflated, 0);
        }
    }
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0xffff_ffff);
    for (let index = 2; index < 6; ++index) {
        appendU32(inflated, (index & 1) === 0 ? 0x4000_0281 : 0x4000_0285);
    }

    const appendMaterial = ({
        name,
        techniqueAlias,
        imageReference,
        includeImage,
        includeConstant,
        imageName = colorMapName,
        imageExternal = externalColorMap,
    }) => {
        const material = new Array(80).fill(0);
        setU32(material, 0, 0xffff_ffff);
        material[24] = 0;
        material.fill(0xff, 25, 58);
        material[58] = 1;
        material[59] = includeConstant ? 1 : 0;
        material[60] = 1;
        setU32(material, 64, techniqueAlias);
        setU32(material, 68, 0xffff_ffff);
        setU32(material, 72, includeConstant ? 0xffff_ffff : 0);
        setU32(material, 76, 0xffff_ffff);
        inflated.push(...material, ...Buffer.from(name, "ascii"), 0);
        appendU32(inflated, 0x1234_5678);
        inflated.push("c".charCodeAt(0), "p".charCodeAt(0), 1, 2);
        appendU32(inflated, imageReference);
        if (includeImage) {
            const image = new Array(36).fill(0);
            if (imageExternal) {
                setU32(image, 0, 3);
                setU32(image, 4, 0xffff_fffe);
                setU16(image, 24, 4);
                setU16(image, 26, 4);
                setU16(image, 28, 1);
            }
            setU32(image, 32, 0xffff_ffff);
            inflated.push(
                ...image,
                ...Buffer.from(
                    imageExternal ? imageName : ",$identitynormalmap",
                    "ascii",
                ),
                0,
            );
            if (imageExternal) {
                appendU16(inflated, 0);
                appendU16(inflated, 4);
                appendU16(inflated, 4);
                appendU16(inflated, 1);
                appendU32(inflated, 0x3154_5844);
                appendU32(inflated, 0);
            }
        }
        if (includeConstant) {
            const constant = new Array(32).fill(0);
            setU32(constant, 0, 0x9abc_def0);
            constant.splice(4, 9, ...Buffer.from("colorTint", "ascii"));
            for (let index = 0; index < 4; ++index) {
                setF32(constant, 16 + index * 4, 1);
            }
            inflated.push(...constant);
        }
        inflated.push(0, 1, 2, 3, 4, 5, 6, 7);
    };
    appendMaterial({
        name: "web/material_a",
        techniqueAlias: 0x4000_0015,
        imageReference: 0xffff_ffff,
        includeImage: true,
        includeConstant: true,
    });
    appendMaterial({
        name: "web/material_b",
        techniqueAlias: 0x4000_001d,
        imageReference: invalidXModelMaterialAlias
            ? 0x4000_0001
            : secondExternalColorMap ? 0xffff_ffff : 0x4000_02b1,
        includeImage: secondExternalColorMap,
        includeConstant: false,
        imageName: secondColorMapName,
        imageExternal: secondExternalColorMap,
    });

    const collisionSurface = new Array(44).fill(0);
    setU32(collisionSurface, 0, 0xffff_ffff);
    setU32(collisionSurface, 4, 1);
    setF32(collisionSurface, 8, invalidXModelCollisionBounds ? 2 : -1);
    setF32(collisionSurface, 12, -1);
    setF32(collisionSurface, 16, -1);
    setF32(collisionSurface, 20, 1);
    setF32(collisionSurface, 24, 1);
    setF32(collisionSurface, 28, 1);
    setU32(collisionSurface, 36, 1);
    inflated.push(...collisionSurface);
    for (let index = 0; index < 12; ++index) {
        appendU32(inflated, index === 0 ? 0x3f80_0000 : 0);
    }
    const boneInfo = new Array(40).fill(0);
    setF32(boneInfo, 0, invalidXModelBoneInfo ? 2 : -1);
    setF32(boneInfo, 4, -1);
    setF32(boneInfo, 8, -1);
    setF32(boneInfo, 12, 1);
    setF32(boneInfo, 16, 1);
    setF32(boneInfo, 20, 1);
    setF32(boneInfo, 36, 3);
    inflated.push(...boneInfo);
    if (xModelPhysPreset) {
        const preset = new Array(44).fill(0);
        setU32(preset, 0, 0xffff_ffff);
        setF32(preset, 8,
            invalidXModelPhysPresetValues ? Number.NaN : 100);
        setF32(preset, 12, 0.25);
        setF32(preset, 16, 0.5);
        setF32(preset, 20, 1);
        setF32(preset, 24, 2);
        setU32(preset, 28, 0xffff_ffff);
        setF32(preset, 32, 0.4);
        setF32(preset, 36, 12);
        preset[40] = 1;
        inflated.push(
            ...preset,
            ...Buffer.from("web/phys_sandbag", "ascii"),
            0,
            ...Buffer.from(
                invalidXModelPhysPresetSoundAlias
                    ? "bad sound" : "sandbag",
                "ascii",
            ),
            0,
        );
    }
    const postXModelTechniqueSet = new Array(148).fill(0);
    setU32(postXModelTechniqueSet, 0, 0xffff_ffff);
    if (invalidPostXModelTechniqueSet) postXModelTechniqueSet[5] = 1;
    if (postXModelTechniqueDependency) {
        setU32(postXModelTechniqueSet, 12 + 4 * 4, 0xffff_ffff);
    }
    inflated.push(
        ...postXModelTechniqueSet,
        ...Buffer.from(",web/mc_l_sm_r0c0n0s0", "ascii"),
        0,
    );
    const laterPostXModelTechniqueSet = new Array(148).fill(0);
    setU32(laterPostXModelTechniqueSet, 0, 0xffff_ffff);
    if (invalidLaterPostXModelTechniqueSet) {
        laterPostXModelTechniqueSet[5] = 1;
    }
    if (laterPostXModelTechniqueDependency) {
        setU32(laterPostXModelTechniqueSet, 12 + 7 * 4, 0xffff_ffff);
    }
    inflated.push(
        ...laterPostXModelTechniqueSet,
        ...Buffer.from(",web/mc_l_sm_r0c0n0s1", "ascii"),
        0,
    );
    const secondXModel = new Array(220).fill(0);
    setU32(secondXModel, 0, 0xffff_ffff);
    secondXModel[4] = 1;
    secondXModel[5] = 1;
    secondXModel[6] = 3;
    setU32(secondXModel, 8,
        unsupportedSecondXModelBoneNames ? 0x4000_0001 : 0xffff_ffff);
    setU32(secondXModel, 24, 0xffff_ffff);
    setU32(secondXModel, 28, 0xffff_ffff);
    setU32(secondXModel, 32, 0xffff_ffff);
    setU32(secondXModel, 36, 0xffff_ffff);
    setF32(secondXModel, 40, 1200);
    setU16(secondXModel, 44, 3);
    setU32(secondXModel, 48, 0x8000_0000);
    setU32(secondXModel, 152, 0xffff_ffff);
    setU32(secondXModel, 156, 1);
    setU32(secondXModel, 164, 0xffff_ffff);
    setF32(secondXModel, 168, 20);
    setF32(secondXModel, 172, invalidSecondXModelBounds ? 3 : -2);
    setF32(secondXModel, 176, -3);
    setF32(secondXModel, 180, -4);
    setF32(secondXModel, 184, 2);
    setF32(secondXModel, 188, 3);
    setF32(secondXModel, 192, 4);
    setU16(secondXModel, 196, 1);
    setU16(secondXModel, 198, 0);
    setU32(secondXModel, 204, 512);
    inflated.push(
        ...secondXModel,
        ...Buffer.from("web/xmodel_second", "ascii"),
        0,
    );
    appendU16(inflated, 0);
    inflated.push(0);
    const secondBaseMat = new Array(32).fill(0);
    setF32(secondBaseMat, 12, 1);
    setF32(secondBaseMat, 28, 1);
    inflated.push(...secondBaseMat);
    for (let index = 0; index < 3; ++index) {
        const surface = new Array(56).fill(0);
        setU16(surface, 2, 3);
        setU16(surface, 4, 1);
        setU16(surface, 8, index);
        setU16(surface, 10, index * 3);
        setU32(surface, 12,
            invalidSecondXSurfaceLayout && index === 0
                ? 0 : 0xffff_ffff);
        setU32(surface, 28, 0xffff_ffff);
        setU32(surface, 32, 1);
        setU32(surface, 36, 0xffff_ffff);
        setU32(surface, 40, 0x8000_0000);
        inflated.push(...surface);
    }
    for (let index = 0; index < 3; ++index) {
        const offset = index * 1.5;
        const packedVertices = [
            [offset - 0.5, -0.5, 0, 0x0000, 0x0000],
            [offset + 0.5, -0.5, 0, 0x3c00, 0x0000],
            [offset, 0.5, 0, 0x0000, 0x3c00],
        ];
        for (const [x, y, z, u, v] of packedVertices) {
            const vertex = new Array(32).fill(0);
            setF32(vertex, 0, x);
            setF32(vertex, 4, y);
            setF32(vertex, 8, z);
            setF32(vertex, 12, 1);
            setU32(vertex, 16, 0xffff_ffff);
            setU32(vertex, 20, (u << 16) | v);
            setU32(vertex, 24, 0x7f7f_ffff);
            setU32(vertex, 28, 0x7f7f_ffff);
            inflated.push(...vertex);
        }
        appendU16(inflated, 0);
        appendU16(inflated, 3);
        appendU16(inflated, 0);
        appendU16(inflated, 1);
        appendU32(inflated, 0);
        appendU16(inflated, 0);
        appendU16(inflated, 1);
        appendU16(inflated, 2);
    }
    const secondMaterialHandleAlias = secondExternalColorMap
        ? 0x4000_04f5 : 0x4000_04f1;
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, secondMaterialHandleAlias);
    appendU32(inflated, secondMaterialHandleAlias);
    const secondMaterial = new Array(80).fill(0);
    setU32(secondMaterial, 0, 0xffff_ffff);
    secondMaterial.fill(0xff, 24, 58);
    secondMaterial[58] = 1;
    setU32(secondMaterial, 64,
        invalidSecondXModelMaterialAlias ? 0x4000_0001 : 0x4000_0015);
    setU32(secondMaterial, 68, 0xffff_ffff);
    inflated.push(
        ...secondMaterial,
        ...Buffer.from("web/material_second", "ascii"),
        0,
    );
    appendU32(inflated, 0x1234_5678);
    inflated.push("c".charCodeAt(0), "p".charCodeAt(0), 1, 2);
    appendU32(inflated,
        invalidSecondXModelImageAlias ? 0x4000_0001 : 0x4000_02b1);
    const secondCollisionSurface = new Array(44).fill(0);
    setU32(secondCollisionSurface, 0, 0xffff_ffff);
    setU32(secondCollisionSurface, 4, 1);
    setF32(secondCollisionSurface, 8, -1);
    setF32(secondCollisionSurface, 12, -1);
    setF32(secondCollisionSurface, 16, -1);
    setF32(secondCollisionSurface, 20, 1);
    setF32(secondCollisionSurface, 24, 1);
    setF32(secondCollisionSurface, 28, 1);
    inflated.push(...secondCollisionSurface);
    for (let index = 0; index < 12; ++index) {
        appendU32(inflated, index === 0 ? 0x3f80_0000 : 0);
    }
    const secondBoneInfo = new Array(40).fill(0);
    setF32(secondBoneInfo, 0, -1);
    setF32(secondBoneInfo, 4, -1);
    setF32(secondBoneInfo, 8, -1);
    setF32(secondBoneInfo, 12, 1);
    setF32(secondBoneInfo, 16, 1);
    setF32(secondBoneInfo, 20, 1);
    setF32(secondBoneInfo, 36, 3);
    inflated.push(...secondBoneInfo);
    if (completePostXModelTechniqueDependencies) {
        const trailingTechniqueSet = new Array(148).fill(0);
        setU32(trailingTechniqueSet, 0, 0xffff_ffff);
        setU32(trailingTechniqueSet, 12 + 4 * 4, 0xffff_ffff);
        setU32(trailingTechniqueSet, 12 + 28 * 4, 0xffff_ffff);
        inflated.push(
            ...trailingTechniqueSet,
            ...Buffer.from(",web/reusable_xmodel_loader_tail", "ascii"),
            0,
        );
        appendWorldMaterialTechnique(inflated, "web/reusable_first");
        appendWorldMaterialTechnique(inflated, "web/reusable_second",
            invalidSecondPostXModelTechnique);
    }
    const compressed = deflateSync(Uint8Array.from(inflated), { level: 9 });
    return Uint8Array.from([
        0x49, 0x57, 0x66, 0x66, 0x75, 0x31, 0x30, 0x30,
        0x05, 0x00, 0x00, 0x00,
        ...compressed,
    ]);
}

export function createSyntheticFxInventoryFastfile()
{
    const inflated = [];
    appendU32(inflated, 4096);
    appendU32(inflated, 0);
    for (const size of [1024 * 1024, 0, 0, 0, 1024 * 1024, 0, 0, 0, 0]) {
        appendU32(inflated, size);
    }
    appendU32(inflated, 0);
    appendU32(inflated, 0);
    appendU32(inflated, 3);
    appendU32(inflated, 0xffff_ffff);
    for (const type of [5, 25, 16]) {
        appendU32(inflated, type);
        appendU32(inflated, 0xffff_ffff);
    }
    const techniqueSet = new Array(148).fill(0);
    setU32(techniqueSet, 0, 0xffff_ffff);
    inflated.push(
        ...techniqueSet,
        ...Buffer.from(",web/fx_prefix", "ascii"),
        0,
    );
    for (const value of [0xffff_ffff, 0, 512, 0, 0, 1, 0, 0xffff_ffff]) {
        appendU32(inflated, value);
    }
    inflated.push(...Buffer.from("web/fx_mark", "ascii"), 0);
    const elem = new Array(252).fill(0);
    elem[176] = 9;
    elem[177] = 1;
    setU32(elem, 188, 0xffff_ffff);
    inflated.push(...elem);
    appendU32(inflated, 0xffff_ffff);
    appendU32(inflated, 0xffff_ffff);
    for (const name of [",web/fx_mark_world", ",web/fx_mark_model"]) {
        const material = new Array(80).fill(0);
        setU32(material, 0, 0xffff_ffff);
        inflated.push(...material, ...Buffer.from(name, "ascii"), 0);
    }
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
        language = "english",
        localization = SYNTHETIC_LOCALIZATION.replace(/^english/, language),
        primaryIwd = createSyntheticIwd(),
        overrides = new Map(),
        omit = [],
        extraFiles = new Map(),
    } = {},
)
{
    const directory = testInfo.outputPath(name);
    const omitted = new Set(omit);
    for (const requirement of getRequiredAssets(language)) {
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
            } else if (requirement.path === "main/iw_13.iwd") {
                // Canonical FS_InitFilesystem validates the base path through
                // this ordinary archive lookup. Keep the legal fixture in a
                // later base IWD so tests remain free to replace iw_00.
                contents = createSyntheticIwd([{
                    path: "fileSysCheck.cfg",
                    contents: "synthetic filesystem validation\n",
                    method: "deflate",
                }, {
                    path: "configure_mp.csv",
                    contents: "cpu ghz,sys mb,r_fullscreen\n0,128,0\ngpu,r_fullscreen\n*,0\n",
                    method: "deflate",
                }]);
            } else if (requirement.kind === "iwd") {
                contents = createSyntheticIwd();
            } else if (requirement.path === `zone/${language}/code_post_gfx.ff`) {
                contents = createSyntheticRetailCensusFastfile();
            } else if (requirement.path === `zone/${language}/common.ff`) {
                contents = createSyntheticEmptyPrerequisiteFastfile();
            } else if (requirement.path === `zone/${language}/ui.ff`) {
                contents = createSyntheticGeneratedPrefixFastfile();
            } else if (requirement.path === `zone/${language}/killhouse.ff`) {
                contents = createSyntheticWorldInventoryFastfile();
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
    for (const [relativePath, contents] of extraFiles) {
        const target = path.join(directory, ...relativePath.split("/"));
        await mkdir(path.dirname(target), { recursive: true });
        await writeFile(target, contents);
    }
    return directory;
}
