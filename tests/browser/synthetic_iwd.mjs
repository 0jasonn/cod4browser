import { crc32, deflateRawSync } from "node:zlib";

export const ZIP_METHOD_STORE = 0;
export const ZIP_METHOD_DEFLATE = 8;
export const IWI_HEADER_SIZE = 28;
export const IWI_VERSION_COD4 = 6;
export const IWI_FORMAT_ARGB = 1;
export const IWI_FORMAT_DXT1 = 11;
export const IWI_FORMAT_DXT3 = 12;
export const IWI_FORMAT_DXT5 = 13;
export const IWI_FLAG_NO_MIPMAPS = 0x02;

const ZIP_LOCAL_SIGNATURE = 0x04034b50;
const ZIP_CENTRAL_SIGNATURE = 0x02014b50;
const ZIP_EOCD_SIGNATURE = 0x06054b50;
const ZIP_VERSION_20 = 20;
const ZIP_UTF8_FLAG = 0x0800;
const UINT16_MAX = 0xffff;
const UINT32_MAX = 0xffffffff;

function asBuffer(value, label)
{
    if (typeof value === "string") {
        return Buffer.from(value, "utf8");
    }
    if (Buffer.isBuffer(value)) {
        return Buffer.from(value);
    }
    if (value instanceof ArrayBuffer) {
        return Buffer.from(value);
    }
    if (ArrayBuffer.isView(value)) {
        return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
    }
    throw new TypeError(`${label} must be a string, Buffer, ArrayBuffer, or typed array.`);
}

function requireUint32(value, label)
{
    if (!Number.isInteger(value) || value < 0 || value > UINT32_MAX) {
        throw new RangeError(`${label} must be an unsigned 32-bit integer.`);
    }
    return value;
}

function requireUint8(value, label)
{
    if (!Number.isInteger(value) || value < 0 || value > 0xff) {
        throw new RangeError(`${label} must be an unsigned 8-bit integer.`);
    }
    return value;
}

function requireInt16(value, label)
{
    if (!Number.isInteger(value) || value < -0x8000 || value > 0x7fff) {
        throw new RangeError(`${label} must be a signed 16-bit integer.`);
    }
    return value;
}

function requireInt32(value, label)
{
    if (!Number.isInteger(value) || value < -0x8000_0000 || value > 0x7fff_ffff) {
        throw new RangeError(`${label} must be a signed 32-bit integer.`);
    }
    return value;
}

// The fixture mirrors the 28-byte GfxImageFileHeader consumed by COD4's
// load-from-file image path. Pixel bytes are entirely caller-provided synthetic
// data; the helper never reads or derives content from a game installation.
export function createSyntheticIwi({
    tag = "IWi",
    version = IWI_VERSION_COD4,
    format = IWI_FORMAT_ARGB,
    flags = IWI_FLAG_NO_MIPMAPS,
    width = 2,
    height = 2,
    depth = 1,
    payload = Buffer.from([
        0xff, 0x20, 0x40, 0x60,
        0xff, 0x80, 0xa0, 0xc0,
        0xff, 0xe0, 0x30, 0x50,
        0xff, 0x70, 0x90, 0xb0,
    ]),
    fileSizeForPicmip = undefined,
} = {})
{
    const tagBytes = asBuffer(tag, "Synthetic IWI tag");
    if (tagBytes.length !== 3) {
        throw new RangeError("Synthetic IWI tags must contain exactly three bytes.");
    }
    const imagePayload = asBuffer(payload, "Synthetic IWI payload");
    const totalSize = IWI_HEADER_SIZE + imagePayload.length;
    if (totalSize > 0x7fff_ffff) {
        throw new RangeError("Synthetic IWI size must fit a signed 32-bit header field.");
    }

    const picmipSizes = fileSizeForPicmip ?? [totalSize, totalSize, totalSize, totalSize];
    if (!Array.isArray(picmipSizes) || picmipSizes.length !== 4) {
        throw new TypeError("Synthetic IWI fileSizeForPicmip must contain four values.");
    }

    const header = Buffer.alloc(IWI_HEADER_SIZE);
    tagBytes.copy(header, 0);
    header.writeUInt8(requireUint8(version, "Synthetic IWI version"), 3);
    header.writeUInt8(requireUint8(format, "Synthetic IWI format"), 4);
    header.writeUInt8(requireUint8(flags, "Synthetic IWI flags"), 5);
    header.writeInt16LE(requireInt16(width, "Synthetic IWI width"), 6);
    header.writeInt16LE(requireInt16(height, "Synthetic IWI height"), 8);
    header.writeInt16LE(requireInt16(depth, "Synthetic IWI depth"), 10);
    picmipSizes.forEach((size, index) => {
        header.writeInt32LE(requireInt32(size, `Synthetic IWI picmip size ${index}`), 12 + index * 4);
    });
    return Buffer.concat([header, imagePayload]);
}

function normalizeMethod(method)
{
    if (method === undefined || method === "store" || method === ZIP_METHOD_STORE) {
        return ZIP_METHOD_STORE;
    }
    if (method === "deflate" || method === ZIP_METHOD_DEFLATE) {
        return ZIP_METHOD_DEFLATE;
    }
    throw new RangeError(`Synthetic ZIP entries only support store (0) and deflate (8), not ${method}.`);
}

function normalizeEntry(entry, index)
{
    if (!entry || typeof entry !== "object" || Array.isArray(entry)) {
        throw new TypeError(`Synthetic ZIP entry ${index} must be an object.`);
    }
    if (typeof entry.path !== "string" || entry.path.length === 0) {
        throw new TypeError(`Synthetic ZIP entry ${index} must have a non-empty path.`);
    }

    const name = Buffer.from(entry.path, "utf8");
    if (name.length > UINT16_MAX) {
        throw new RangeError(`Synthetic ZIP entry ${index} has a path longer than ZIP32 permits.`);
    }

    const contents = asBuffer(entry.contents ?? Buffer.alloc(0), `Entry ${entry.path} contents`);
    const method = normalizeMethod(entry.method);
    const compressed = entry.compressedBytes === undefined
        ? method === ZIP_METHOD_DEFLATE
            ? deflateRawSync(contents, { level: 9 })
            : Buffer.from(contents)
        : asBuffer(entry.compressedBytes, `Entry ${entry.path} compressed bytes`);
    requireUint32(contents.length, `Entry ${entry.path} uncompressed size`);
    requireUint32(compressed.length, `Entry ${entry.path} compressed size`);

    const declaredCompressedSize = entry.declaredCompressedSize === undefined
        ? compressed.length
        : requireUint32(entry.declaredCompressedSize, `Entry ${entry.path} declared compressed size`);
    const declaredUncompressedSize = entry.declaredUncompressedSize === undefined
        ? contents.length
        : requireUint32(entry.declaredUncompressedSize, `Entry ${entry.path} declared uncompressed size`);

    const calculatedCrc32 = crc32(contents) >>> 0;
    const declaredCrc32 = entry.declaredCrc32 === undefined
        ? calculatedCrc32
        : requireUint32(entry.declaredCrc32, `Entry ${entry.path} declared CRC-32`);
    const flags = name.some((byte) => byte >= 0x80) ? ZIP_UTF8_FLAG : 0;

    return {
        path: entry.path,
        name,
        contents,
        compressed,
        method,
        flags,
        calculatedCrc32,
        declaredCrc32,
        declaredCompressedSize,
        declaredUncompressedSize,
    };
}

// Every emitted byte is derived from caller-provided synthetic strings/bytes.
// Header timestamps and attributes are fixed at zero so repeated runs are
// byte-for-byte deterministic and never depend on a local COD4 installation.
export function createSyntheticIwd(entries = undefined, { comment = Buffer.alloc(0) } = {})
{
    const normalizedEntries = (entries ?? [
        { path: "x", contents: Buffer.alloc(0), method: ZIP_METHOD_STORE },
    ]).map(normalizeEntry);
    if (normalizedEntries.length > UINT16_MAX) {
        throw new RangeError("Synthetic ZIP32 fixtures cannot contain more than 65,535 entries.");
    }

    const localRecords = [];
    const centralRecords = [];
    let localOffset = 0;

    for (const entry of normalizedEntries) {
        requireUint32(localOffset, `Entry ${entry.path} local-header offset`);

        const local = Buffer.alloc(30 + entry.name.length);
        local.writeUInt32LE(ZIP_LOCAL_SIGNATURE, 0);
        local.writeUInt16LE(ZIP_VERSION_20, 4);
        local.writeUInt16LE(entry.flags, 6);
        local.writeUInt16LE(entry.method, 8);
        local.writeUInt32LE(entry.declaredCrc32, 14);
        local.writeUInt32LE(entry.declaredCompressedSize, 18);
        local.writeUInt32LE(entry.declaredUncompressedSize, 22);
        local.writeUInt16LE(entry.name.length, 26);
        local.writeUInt16LE(0, 28);
        entry.name.copy(local, 30);
        const localRecord = Buffer.concat([local, entry.compressed]);
        localRecords.push(localRecord);

        const central = Buffer.alloc(46 + entry.name.length);
        central.writeUInt32LE(ZIP_CENTRAL_SIGNATURE, 0);
        central.writeUInt16LE(ZIP_VERSION_20, 4);
        central.writeUInt16LE(ZIP_VERSION_20, 6);
        central.writeUInt16LE(entry.flags, 8);
        central.writeUInt16LE(entry.method, 10);
        central.writeUInt32LE(entry.declaredCrc32, 16);
        central.writeUInt32LE(entry.declaredCompressedSize, 20);
        central.writeUInt32LE(entry.declaredUncompressedSize, 24);
        central.writeUInt16LE(entry.name.length, 28);
        central.writeUInt16LE(0, 30);
        central.writeUInt16LE(0, 32);
        central.writeUInt16LE(0, 34);
        central.writeUInt16LE(0, 36);
        central.writeUInt32LE(0, 38);
        central.writeUInt32LE(localOffset, 42);
        entry.name.copy(central, 46);
        centralRecords.push(central);

        localOffset += localRecord.length;
    }

    const centralDirectory = Buffer.concat(centralRecords);
    requireUint32(localOffset, "Synthetic ZIP central-directory offset");
    requireUint32(centralDirectory.length, "Synthetic ZIP central-directory size");

    const zipComment = asBuffer(comment, "Synthetic ZIP comment");
    if (zipComment.length > UINT16_MAX) {
        throw new RangeError("Synthetic ZIP comments cannot exceed 65,535 bytes.");
    }

    const eocd = Buffer.alloc(22 + zipComment.length);
    eocd.writeUInt32LE(ZIP_EOCD_SIGNATURE, 0);
    eocd.writeUInt16LE(0, 4);
    eocd.writeUInt16LE(0, 6);
    eocd.writeUInt16LE(normalizedEntries.length, 8);
    eocd.writeUInt16LE(normalizedEntries.length, 10);
    eocd.writeUInt32LE(centralDirectory.length, 12);
    eocd.writeUInt32LE(localOffset, 16);
    eocd.writeUInt16LE(zipComment.length, 20);
    zipComment.copy(eocd, 22);

    return Buffer.concat([...localRecords, centralDirectory, eocd]);
}
