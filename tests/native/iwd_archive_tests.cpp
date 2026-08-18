#include <qcommon/iwd_archive.h>
#if defined(KISAK_TEST_CANONICAL_MINIZIP)
#include <qcommon/unzip.h>
#endif
#include "zlib_test_support.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(KISAK_TEST_CANONICAL_MINIZIP)
void *Z_Malloc(int size, const char *, int)
{
    return std::malloc(static_cast<std::size_t>(size));
}

void Z_Free(void *pointer, int)
{
    std::free(pointer);
}

void Com_Memcpy(void *destination, const void *source, const std::size_t count)
{
    std::memcpy(destination, source, count);
}

uint32_t __cdecl FS_FileRead(void *pointer, uint32_t length, FILE *stream)
{
    return static_cast<uint32_t>(std::fread(pointer, 1, length, stream));
}

FILE *__cdecl FS_FileOpenReadBinary(const char *filename)
{
    return std::fopen(filename, "rb");
}

void __cdecl FS_FileClose(FILE *stream)
{
    if (stream) std::fclose(stream);
}

int __cdecl FS_FileSeek(FILE *stream, int offset, int origin)
{
    // Kisak's file wrapper uses 0=current, 1=end, and 2=beginning.
    static constexpr int origins[] = {SEEK_CUR, SEEK_END, SEEK_SET};
    return origin >= 0 && origin < 3
        ? std::fseek(stream, offset, origins[origin])
        : -1;
}

int __cdecl FS_FileGetFileSize(FILE *stream)
{
    const long position = std::ftell(stream);
    if (position < 0 || std::fseek(stream, 0, SEEK_END) != 0) return -1;
    const long size = std::ftell(stream);
    if (std::fseek(stream, position, SEEK_SET) != 0 || size < 0 ||
        size > std::numeric_limits<int>::max()) return -1;
    return static_cast<int>(size);
}
#endif

namespace
{
using Bytes = std::vector<uint8_t>;
using kisak::iwd::ArchiveIndex;
using kisak::iwd::CentralDirectoryLocator;
using kisak::iwd::DecoderProgress;
using kisak::iwd::Entry;
using kisak::iwd::Error;
using kisak::iwd::Limits;
using kisak::iwd::MemberDecoder;
using kisak::iwd::MemberLocation;

constexpr uint32_t LOCAL_SIGNATURE = 0x04034b50u;
constexpr uint32_t CENTRAL_SIGNATURE = 0x02014b50u;
constexpr uint32_t EOCD_SIGNATURE = 0x06054b50u;

class TestFailure final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw TestFailure(std::string(message));
    }
}

void RequireError(Error actual, Error expected, std::string_view context)
{
    if (actual == expected)
    {
        return;
    }
    std::string message(context);
    message += ": expected ";
    message += kisak::iwd::ErrorString(expected);
    message += ", got ";
    message += kisak::iwd::ErrorString(actual);
    throw TestFailure(message);
}

void AppendU16(Bytes &bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void AppendU32(Bytes &bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 24));
}

void PatchU16(Bytes &bytes, std::size_t offset, uint16_t value)
{
    Require(offset <= bytes.size() && bytes.size() - offset >= 2, "u16 patch is in range");
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void PatchU32(Bytes &bytes, std::size_t offset, uint32_t value)
{
    Require(offset <= bytes.size() && bytes.size() - offset >= 4, "u32 patch is in range");
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

uint32_t Crc(std::span<const uint8_t> bytes)
{
    return static_cast<uint32_t>(crc32(
        crc32(0L, Z_NULL, 0),
        reinterpret_cast<const Bytef *>(bytes.data()),
        static_cast<uInt>(bytes.size())));
}

Bytes ToBytes(std::string_view text)
{
    return Bytes(text.begin(), text.end());
}

Bytes DeflateRaw(std::span<const uint8_t> input)
{
    z_stream stream{};
    if (deflateInit2(
            &stream,
            Z_BEST_COMPRESSION,
            Z_DEFLATED,
            -MAX_WBITS,
            8,
            Z_DEFAULT_STRATEGY) != Z_OK)
    {
        throw TestFailure("fixture raw-deflate initialization failed");
    }

    Bytes output(std::max<std::size_t>(
        64, KisakTestCompressBound(static_cast<uLong>(input.size()))));
    stream.next_in = input.empty()
        ? Z_NULL
        : const_cast<Bytef *>(reinterpret_cast<const Bytef *>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = deflate(&stream, Z_FINISH);
    const std::size_t produced = stream.total_out;
    deflateEnd(&stream);
    if (result != Z_STREAM_END)
    {
        throw TestFailure("fixture raw-deflate did not finish");
    }
    output.resize(produced);
    return output;
}

struct EntrySpec
{
    std::string path;
    Bytes contents;
    uint16_t method = 0;
    uint16_t flags = 0;
    uint16_t versionNeeded = 20;
    uint16_t modificationTime = 0;
    uint16_t modificationDate = 0;
    uint16_t diskStart = 0;
    Bytes localExtra;
    Bytes centralExtra;
    std::optional<Bytes> compressedData;
    std::optional<uint32_t> declaredCrc;
    std::optional<uint32_t> declaredCompressedSize;
    std::optional<uint32_t> declaredUncompressedSize;
};

EntrySpec Stored(std::string path, std::string_view contents)
{
    EntrySpec entry;
    entry.path = std::move(path);
    entry.contents = ToBytes(contents);
    entry.method = 0;
    return entry;
}

EntrySpec Deflated(std::string path, std::string_view contents)
{
    EntrySpec entry;
    entry.path = std::move(path);
    entry.contents = ToBytes(contents);
    entry.method = 8;
    return entry;
}

struct Fixture
{
    Bytes archive;
    uint32_t centralOffset = 0;
    uint32_t centralSize = 0;
    uint32_t eocdOffset = 0;
    std::vector<uint32_t> localOffsets;
    std::vector<uint32_t> dataOffsets;
    std::vector<uint32_t> centralRecordOffsets;
};

struct PreparedEntry
{
    EntrySpec spec;
    Bytes compressed;
    uint32_t crc = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint32_t localOffset = 0;
};

Fixture BuildFixture(std::vector<EntrySpec> specifications, Bytes comment = {})
{
    Require(!specifications.empty(), "fixture has at least one entry");
    Require(specifications.size() < 0xffffu, "fixture entry count fits ZIP32");
    Require(comment.size() <= 0xffffu, "fixture comment fits ZIP32");

    Fixture fixture;
    std::vector<PreparedEntry> entries;
    entries.reserve(specifications.size());
    for (EntrySpec &specification : specifications)
    {
        Require(specification.path.size() <= 0xffffu, "fixture path fits ZIP32");
        Require(specification.localExtra.size() <= 0xffffu, "fixture local extra fits ZIP32");
        Require(specification.centralExtra.size() <= 0xffffu, "fixture central extra fits ZIP32");

        PreparedEntry entry;
        entry.spec = std::move(specification);
        if (entry.spec.compressedData)
        {
            entry.compressed = *entry.spec.compressedData;
        }
        else if (entry.spec.method == 8)
        {
            entry.compressed = DeflateRaw(entry.spec.contents);
        }
        else
        {
            entry.compressed = entry.spec.contents;
        }
        entry.crc = entry.spec.declaredCrc.value_or(Crc(entry.spec.contents));
        entry.compressedSize = entry.spec.declaredCompressedSize.value_or(
            static_cast<uint32_t>(entry.compressed.size()));
        entry.uncompressedSize = entry.spec.declaredUncompressedSize.value_or(
            static_cast<uint32_t>(entry.spec.contents.size()));
        entries.push_back(std::move(entry));
    }

    for (PreparedEntry &entry : entries)
    {
        entry.localOffset = static_cast<uint32_t>(fixture.archive.size());
        fixture.localOffsets.push_back(entry.localOffset);
        AppendU32(fixture.archive, LOCAL_SIGNATURE);
        AppendU16(fixture.archive, entry.spec.versionNeeded);
        AppendU16(fixture.archive, entry.spec.flags);
        AppendU16(fixture.archive, entry.spec.method);
        AppendU16(fixture.archive, entry.spec.modificationTime);
        AppendU16(fixture.archive, entry.spec.modificationDate);
        AppendU32(fixture.archive, entry.crc);
        AppendU32(fixture.archive, entry.compressedSize);
        AppendU32(fixture.archive, entry.uncompressedSize);
        AppendU16(fixture.archive, static_cast<uint16_t>(entry.spec.path.size()));
        AppendU16(fixture.archive, static_cast<uint16_t>(entry.spec.localExtra.size()));
        fixture.archive.insert(
            fixture.archive.end(), entry.spec.path.begin(), entry.spec.path.end());
        fixture.archive.insert(
            fixture.archive.end(), entry.spec.localExtra.begin(), entry.spec.localExtra.end());
        fixture.dataOffsets.push_back(static_cast<uint32_t>(fixture.archive.size()));
        fixture.archive.insert(
            fixture.archive.end(), entry.compressed.begin(), entry.compressed.end());
    }

    fixture.centralOffset = static_cast<uint32_t>(fixture.archive.size());
    for (const PreparedEntry &entry : entries)
    {
        fixture.centralRecordOffsets.push_back(static_cast<uint32_t>(fixture.archive.size()));
        AppendU32(fixture.archive, CENTRAL_SIGNATURE);
        AppendU16(fixture.archive, 20);
        AppendU16(fixture.archive, entry.spec.versionNeeded);
        AppendU16(fixture.archive, entry.spec.flags);
        AppendU16(fixture.archive, entry.spec.method);
        AppendU16(fixture.archive, entry.spec.modificationTime);
        AppendU16(fixture.archive, entry.spec.modificationDate);
        AppendU32(fixture.archive, entry.crc);
        AppendU32(fixture.archive, entry.compressedSize);
        AppendU32(fixture.archive, entry.uncompressedSize);
        AppendU16(fixture.archive, static_cast<uint16_t>(entry.spec.path.size()));
        AppendU16(fixture.archive, static_cast<uint16_t>(entry.spec.centralExtra.size()));
        AppendU16(fixture.archive, 0);
        AppendU16(fixture.archive, entry.spec.diskStart);
        AppendU16(fixture.archive, 0);
        AppendU32(fixture.archive, 0);
        AppendU32(fixture.archive, entry.localOffset);
        fixture.archive.insert(
            fixture.archive.end(), entry.spec.path.begin(), entry.spec.path.end());
        fixture.archive.insert(
            fixture.archive.end(), entry.spec.centralExtra.begin(), entry.spec.centralExtra.end());
    }
    fixture.centralSize = static_cast<uint32_t>(fixture.archive.size()) - fixture.centralOffset;
    fixture.eocdOffset = static_cast<uint32_t>(fixture.archive.size());

    AppendU32(fixture.archive, EOCD_SIGNATURE);
    AppendU16(fixture.archive, 0);
    AppendU16(fixture.archive, 0);
    AppendU16(fixture.archive, static_cast<uint16_t>(entries.size()));
    AppendU16(fixture.archive, static_cast<uint16_t>(entries.size()));
    AppendU32(fixture.archive, fixture.centralSize);
    AppendU32(fixture.archive, fixture.centralOffset);
    AppendU16(fixture.archive, static_cast<uint16_t>(comment.size()));
    fixture.archive.insert(fixture.archive.end(), comment.begin(), comment.end());
    return fixture;
}

#if defined(KISAK_TEST_CANONICAL_MINIZIP)
void TestCanonicalMinizipReadAndSeek()
{
#if defined(__EMSCRIPTEN__)
    constexpr const char *fixturePath = "/canonical-minizip-fixture.iwd";
#else
    constexpr const char *fixturePath = "canonical-minizip-fixture.iwd";
#endif
    const Fixture fixture = BuildFixture({Deflated("seek.txt", "0123456789")});
    {
        std::ofstream output(fixturePath, std::ios::binary | std::ios::trunc);
        Require(output.good(), "canonical minizip fixture opens for writing");
        output.write(
            reinterpret_cast<const char *>(fixture.archive.data()),
            static_cast<std::streamsize>(fixture.archive.size()));
        Require(output.good(), "canonical minizip fixture is written");
    }

    unzFile archive = unzOpen(fixturePath);
    Require(archive != nullptr, "canonical minizip opens generated IWD");
    Require(unzGoToFirstFile(archive) == UNZ_OK, "canonical minizip selects first member");
    unz_file_info info{};
    std::array<char, 32> filename{};
    Require(unzGetCurrentFileInfo(
        archive, &info, filename.data(), filename.size(), nullptr, 0, nullptr, 0) == UNZ_OK,
        "canonical minizip reads member metadata");
    Require(std::string_view(filename.data()) == "seek.txt", "canonical member name matches");
    Require(info.uncompressed_size == 10, "canonical member size matches");
    Require(unzOpenCurrentFile(archive) == UNZ_OK, "canonical member opens");

    Require(unzReadCurrentFile(archive, nullptr, 4) == 4,
        "canonical discard read advances compressed member");
    std::array<char, 6> tail{};
    Require(unzReadCurrentFile(archive, tail.data(), tail.size()) ==
            static_cast<int>(tail.size()),
        "canonical member tail reads after seek");
    Require(std::string_view(tail.data(), tail.size()) == "456789",
        "canonical compressed-member seek preserves bytes");
    Require(unzReadCurrentFile(archive, tail.data(), 1) == 0,
        "canonical member reports end of file");
    Require(unzCloseCurrentFile(archive) == UNZ_OK, "canonical member closes cleanly");
    Require(unzClose(archive) == UNZ_OK, "canonical IWD closes cleanly");
    Require(std::remove(fixturePath) == 0, "canonical minizip fixture is removed");
}
#endif

CentralDirectoryLocator Locate(const Fixture &fixture, const Limits &limits = {})
{
    CentralDirectoryLocator locator;
    const Error error = kisak::iwd::LocateCentralDirectory(
        fixture.archive, 0, fixture.archive.size(), limits, locator);
    RequireError(error, Error::None, "locate fixture central directory");
    return locator;
}

Error Parse(
    const Fixture &fixture,
    const CentralDirectoryLocator &locator,
    ArchiveIndex &index,
    const Limits &limits = {})
{
    return kisak::iwd::ParseCentralDirectory(
        std::span<const uint8_t>(fixture.archive).subspan(
            locator.centralOffset, locator.centralSize),
        locator,
        limits,
        index);
}

ArchiveIndex Parse(const Fixture &fixture, const CentralDirectoryLocator &locator)
{
    ArchiveIndex index;
    RequireError(Parse(fixture, locator, index), Error::None, "parse fixture central directory");
    return index;
}

MemberLocation ValidateMember(
    const Fixture &fixture,
    const CentralDirectoryLocator &locator,
    const Entry &entry)
{
    Require(entry.localHeaderOffset <= fixture.archive.size() &&
        fixture.archive.size() - entry.localHeaderOffset >= 30,
        "fixture contains local-header prefix");
    const std::span<const uint8_t> prefix =
        std::span<const uint8_t>(fixture.archive).subspan(entry.localHeaderOffset, 30);
    uint32_t requiredBytes = 0;
    RequireError(
        kisak::iwd::RequiredLocalHeaderBytes(prefix, requiredBytes),
        Error::None,
        "size local header");
    Require(entry.localHeaderOffset <= fixture.archive.size() &&
        requiredBytes <= fixture.archive.size() - entry.localHeaderOffset,
        "fixture contains complete local header");
    MemberLocation location;
    RequireError(
        kisak::iwd::ValidateLocalHeader(
            entry,
            locator,
            std::span<const uint8_t>(fixture.archive).subspan(
                entry.localHeaderOffset, requiredBytes),
            location),
        Error::None,
        "validate local header");
    return location;
}

struct DecodeResult
{
    Error consumeError = Error::None;
    Error finishError = Error::None;
    Bytes output;
};

DecodeResult Decode(
    const Fixture &fixture,
    const Entry &entry,
    const MemberLocation &location,
    std::size_t inputChunk = 5,
    std::size_t outputChunk = 3)
{
    MemberDecoder decoder;
    RequireError(decoder.Begin(entry), Error::None, "begin member decoder");
    DecodeResult result;
    std::size_t inputOffset = 0;
    std::vector<uint8_t> output(std::max<std::size_t>(outputChunk, 1));

    for (std::size_t iterations = 0; iterations < 100000; ++iterations)
    {
        if (decoder.Progress() == DecoderProgress::ReadyToFinish)
        {
            break;
        }

        const std::size_t remaining = location.compressedSize - inputOffset;
        const std::size_t supplied = std::min(inputChunk, remaining);
        const auto input = std::span<const uint8_t>(fixture.archive).subspan(
            location.dataOffset + inputOffset, supplied);
        std::size_t inputConsumed = 0;
        std::size_t outputProduced = 0;
        result.consumeError = decoder.Consume(
            input,
            std::span<uint8_t>(output).first(outputChunk),
            inputConsumed,
            outputProduced);
        if (result.consumeError != Error::None)
        {
            return result;
        }
        result.output.insert(
            result.output.end(),
            output.begin(),
            output.begin() + static_cast<Bytes::difference_type>(outputProduced));
        inputOffset += inputConsumed;
        if (inputConsumed == 0 && outputProduced == 0)
        {
            break;
        }
    }

    result.finishError = decoder.Finish();
    return result;
}

void ExpectParseError(const Fixture &fixture, Error expected, const Limits &limits = {})
{
    const CentralDirectoryLocator locator = Locate(fixture);
    ArchiveIndex index;
    RequireError(Parse(fixture, locator, index, limits), expected, "parse rejection");
}

void TestHappyPath()
{
    const Fixture fixture = BuildFixture({
        Stored("synthetic/stored.txt", "stored payload"),
        Deflated("synthetic/deflated.txt", "deflated payload deflated payload"),
    }, ToBytes("synthetic comment"));
    const CentralDirectoryLocator locator = Locate(fixture);
    Require(locator.archiveSize == fixture.archive.size(), "locator records archive size");
    Require(locator.centralOffset == fixture.centralOffset, "locator records central offset");
    Require(locator.centralSize == fixture.centralSize, "locator records central size");
    Require(locator.entryCount == 2, "locator records entry count");
    Require(locator.commentSize == 17, "locator records comment size");

    const ArchiveIndex index = Parse(fixture, locator);
    Require(index.RecordCount() == 2, "index records both central records");
    Require(index.Entries().size() == 2, "index exposes two unique paths");
    Require(index.Find("SYNTHETIC/STORED.TXT") != nullptr, "lookup folds ASCII case");
    Require(index.FindExact("SYNTHETIC/STORED.TXT") == nullptr, "exact lookup stays exact");

    for (const Entry &entry : index.Entries())
    {
        const MemberLocation location = ValidateMember(fixture, locator, entry);
        const DecodeResult decoded = Decode(fixture, entry, location);
        RequireError(decoded.consumeError, Error::None, "happy decoder consume");
        RequireError(decoded.finishError, Error::None, "happy decoder finish");
        const Bytes expected = entry.path.find("stored") != std::string::npos
            ? ToBytes("stored payload")
            : ToBytes("deflated payload deflated payload");
        Require(decoded.output == expected, "decoded bytes match fixture contents");
    }
}

void TestLocatorRejections()
{
    const Fixture baseline = BuildFixture({Stored("a.txt", "a")});

    CentralDirectoryLocator locator;
    constexpr std::size_t tailOffset = 1;
    RequireError(
        kisak::iwd::LocateCentralDirectory(
            std::span<const uint8_t>(baseline.archive).subspan(tailOffset),
            tailOffset,
            baseline.archive.size(),
            {},
            locator),
        Error::None,
        "nonzero terminal-tail offset");
    Require(locator.centralOffset == baseline.centralOffset,
        "terminal-tail offset preserves absolute central offset");

    RequireError(
        kisak::iwd::LocateCentralDirectory(
            std::span<const uint8_t>(baseline.archive).subspan(1),
            0,
            baseline.archive.size(),
            {},
            locator),
        Error::TailRange,
        "non-terminal tail");

    Fixture missing = baseline;
    PatchU32(missing.archive, missing.eocdOffset, 0);
    RequireError(
        kisak::iwd::LocateCentralDirectory(
            missing.archive, 0, missing.archive.size(), {}, locator),
        Error::EocdNotFound,
        "missing EOCD signature");

    Fixture multiDisk = baseline;
    PatchU16(multiDisk.archive, multiDisk.eocdOffset + 4, 1);
    RequireError(
        kisak::iwd::LocateCentralDirectory(
            multiDisk.archive, 0, multiDisk.archive.size(), {}, locator),
        Error::MultiDiskUnsupported,
        "multi-disk EOCD");

    Fixture zip64 = baseline;
    PatchU16(zip64.archive, zip64.eocdOffset + 10, 0xffffu);
    RequireError(
        kisak::iwd::LocateCentralDirectory(
            zip64.archive, 0, zip64.archive.size(), {}, locator),
        Error::Zip64Unsupported,
        "ZIP64 EOCD sentinel");

    Limits entryLimit;
    entryLimit.maxEntries = 0;
    RequireError(
        kisak::iwd::LocateCentralDirectory(
            baseline.archive, 0, baseline.archive.size(), entryLimit, locator),
        Error::EntryCountLimit,
        "entry-count locator budget");

    Limits centralLimit;
    centralLimit.maxCentralDirectoryBytes = baseline.centralSize - 1;
    RequireError(
        kisak::iwd::LocateCentralDirectory(
            baseline.archive, 0, baseline.archive.size(), centralLimit, locator),
        Error::CentralDirectoryLimit,
        "central-byte locator budget");

    Fixture badRange = baseline;
    PatchU32(badRange.archive, badRange.eocdOffset + 16, baseline.centralOffset + 1);
    RequireError(
        kisak::iwd::LocateCentralDirectory(
            badRange.archive, 0, badRange.archive.size(), {}, locator),
        Error::CentralDirectoryRange,
        "central range mismatch");
}

void TestCentralMetadataRejections()
{
    struct Case
    {
        const char *name;
        EntrySpec entry;
        Error error;
    };

    EntrySpec encrypted = Stored("a.txt", "a");
    encrypted.flags = 0x0001u;
    EntrySpec descriptor = Stored("a.txt", "a");
    descriptor.flags = 0x0008u;
    EntrySpec unsupportedFlags = Stored("a.txt", "a");
    unsupportedFlags.flags = 0x0100u;
    EntrySpec unsupportedMethod = Stored("a.txt", "a");
    unsupportedMethod.method = 12;
    EntrySpec unsupportedVersion = Stored("a.txt", "a");
    unsupportedVersion.versionNeeded = 21;
    EntrySpec zip64Version = Stored("a.txt", "a");
    zip64Version.versionNeeded = 45;
    EntrySpec multiDisk = Stored("a.txt", "a");
    multiDisk.diskStart = 1;
    EntrySpec storedMismatch = Stored("a.txt", "a");
    storedMismatch.declaredCompressedSize = 2;

    const std::array cases = {
        Case{"encrypted", std::move(encrypted), Error::EncryptedEntry},
        Case{"descriptor", std::move(descriptor), Error::DataDescriptorUnsupported},
        Case{"unsupported flags", std::move(unsupportedFlags), Error::UnsupportedFlags},
        Case{"unsupported method", std::move(unsupportedMethod), Error::UnsupportedCompression},
        Case{"unsupported version", std::move(unsupportedVersion), Error::UnsupportedVersion},
        Case{"ZIP64 version", std::move(zip64Version), Error::Zip64Unsupported},
        Case{"central disk", std::move(multiDisk), Error::MultiDiskUnsupported},
        Case{"stored size mismatch", std::move(storedMismatch), Error::StoredSizeMismatch},
    };
    for (const Case &test : cases)
    {
        const Fixture fixture = BuildFixture({test.entry});
        const CentralDirectoryLocator locator = Locate(fixture);
        ArchiveIndex index;
        const Error actual = Parse(fixture, locator, index);
        if (actual != test.error)
        {
            std::string context = "central metadata case ";
            context += test.name;
            RequireError(actual, test.error, context);
        }
    }

    EntrySpec malformedExtra = Stored("a.txt", "a");
    malformedExtra.centralExtra = {0x34, 0x12, 0x02, 0x00, 0x00};
    ExpectParseError(BuildFixture({malformedExtra}), Error::InvalidExtraField);

    EntrySpec zip64Extra = Stored("a.txt", "a");
    zip64Extra.centralExtra = {0x01, 0x00, 0x00, 0x00};
    ExpectParseError(BuildFixture({zip64Extra}), Error::Zip64Unsupported);
}

void TestPathSafetyTable()
{
    std::vector<std::string> unsafe = {
        "/absolute.txt",
        "C:/drive.txt",
        "folder\\backslash.txt",
        "folder/./dot.txt",
        "folder/../traversal.txt",
        "../leading.txt",
        "folder//empty.txt",
        "folder:alias.txt",
    };
    unsafe.emplace_back("control\x01.txt", 12);
    unsafe.emplace_back("nul\0name.txt", 12);

    for (const std::string &path : unsafe)
    {
        const Fixture fixture = BuildFixture({Stored(path, "x")});
        const CentralDirectoryLocator locator = Locate(fixture);
        ArchiveIndex index;
        const Error actual = Parse(fixture, locator, index);
        if (actual != Error::UnsafePath)
        {
            RequireError(actual, Error::UnsafePath, "unsafe path table");
        }
    }

    EntrySpec invalidUtf8 = Stored(std::string("bad\xc0\x80.txt", 9), "x");
    invalidUtf8.flags = 0x0800u;
    ExpectParseError(BuildFixture({invalidUtf8}), Error::UnsafePath);

    const Fixture directory = BuildFixture({Stored("synthetic/", "")});
    const auto locator = Locate(directory);
    const auto index = Parse(directory, locator);
    Require(index.Entries()[0].directory, "one terminal slash represents a directory");
    MemberDecoder decoder;
    RequireError(decoder.Begin(index.Entries()[0]), Error::DirectoryEntry, "directory decode rejection");
}

void TestCollisionPolicy()
{
    const Fixture collision = BuildFixture({
        Stored("Folder/Name.txt", "first"),
        Stored("folder/name.txt", "second"),
    });
    ExpectParseError(collision, Error::CaseCollision);

    const Fixture duplicate = BuildFixture({
        Stored("same.txt", "first"),
        Deflated("same.txt", "second payload"),
    });
    const CentralDirectoryLocator locator = Locate(duplicate);
    const ArchiveIndex index = Parse(duplicate, locator);
    Require(index.RecordCount() == 2, "duplicate policy counts both records");
    Require(index.Entries().size() == 1, "duplicate policy exposes one unique entry");
    Require(index.TotalDeclaredUncompressedBytes() == 19, "duplicate policy budgets both records");
    const Entry *entry = index.Find("SAME.TXT");
    Require(entry != nullptr, "last duplicate remains findable");
    Require(entry->centralRecord == 1, "last duplicate replaces earlier metadata");
    Require(entry->compressionMethod == 8, "last duplicate method wins");
    const MemberLocation location = ValidateMember(duplicate, locator, *entry);
    const DecodeResult decoded = Decode(duplicate, *entry, location);
    RequireError(decoded.finishError, Error::None, "last duplicate decoder finish");
    Require(decoded.output == ToBytes("second payload"), "last duplicate data wins");
}

void TestBudgets()
{
    const Fixture fixture = BuildFixture({
        Stored("one.txt", "12345"),
        Deflated("two.txt", "abcdefghij"),
    });
    const CentralDirectoryLocator locator = Locate(fixture);

    Limits nameLimit;
    nameLimit.maxPathBytes = 6;
    ArchiveIndex index;
    RequireError(Parse(fixture, locator, index, nameLimit), Error::PathTooLong, "path-byte budget");

    Limits cumulative;
    cumulative.maxCumulativePathBytes = 13;
    RequireError(Parse(fixture, locator, index, cumulative), Error::CumulativePathLimit,
        "cumulative path budget");

    Limits compressed;
    compressed.maxMemberCompressedBytes = 4;
    RequireError(Parse(fixture, locator, index, compressed), Error::MemberCompressedLimit,
        "member compressed budget");

    Limits uncompressed;
    uncompressed.maxMemberUncompressedBytes = 9;
    RequireError(Parse(fixture, locator, index, uncompressed), Error::MemberUncompressedLimit,
        "member uncompressed budget");

    Limits total;
    total.maxTotalUncompressedBytes = 14;
    RequireError(Parse(fixture, locator, index, total), Error::TotalUncompressedLimit,
        "total output budget");

    Limits entries;
    entries.maxEntries = 1;
    CentralDirectoryLocator unused;
    RequireError(
        kisak::iwd::LocateCentralDirectory(
            fixture.archive, 0, fixture.archive.size(), entries, unused),
        Error::EntryCountLimit,
        "entry budget counts all records");
}

void TestCentralFramingAndAtomicity()
{
    const Fixture good = BuildFixture({Stored("good.txt", "good")});
    const CentralDirectoryLocator goodLocator = Locate(good);
    ArchiveIndex index = Parse(good, goodLocator);

    Fixture signature = good;
    PatchU32(signature.archive, signature.centralOffset, 0);
    RequireError(Parse(signature, Locate(signature), index), Error::CentralDirectorySignature,
        "central signature mutation");
    Require(index.FindExact("good.txt") != nullptr, "failed parse preserves prior index");

    Fixture truncated = good;
    PatchU16(truncated.archive, truncated.centralOffset + 28, 0xffffu);
    RequireError(Parse(truncated, Locate(truncated), index), Error::CentralDirectoryTruncated,
        "central variable record truncation");

    Fixture trailing = good;
    trailing.archive.insert(
        trailing.archive.begin() +
            static_cast<Bytes::difference_type>(trailing.eocdOffset),
        0xa5u);
    ++trailing.centralSize;
    ++trailing.eocdOffset;
    PatchU32(trailing.archive, trailing.eocdOffset + 12, trailing.centralSize);
    RequireError(Parse(trailing, Locate(trailing), index), Error::CentralDirectoryTrailingData,
        "central trailing byte");

    CentralDirectoryLocator locator = goodLocator;
    ++locator.centralSize;
    RequireError(
        kisak::iwd::ParseCentralDirectory(
            std::span<const uint8_t>(good.archive).subspan(
                good.centralOffset, good.centralSize),
            locator,
            {},
            index),
        Error::CentralDirectorySizeMismatch,
        "exact central buffer length");
}

void TestLocalHeaderValidation()
{
    const Fixture fixture = BuildFixture({Deflated("local.txt", "local payload")});
    const CentralDirectoryLocator locator = Locate(fixture);
    const ArchiveIndex index = Parse(fixture, locator);
    const Entry &entry = index.Entries()[0];

    uint32_t required = 0;
    RequireError(
        kisak::iwd::RequiredLocalHeaderBytes(
            std::span<const uint8_t>(fixture.archive).first(29), required),
        Error::LocalHeaderTruncated,
        "truncated local prefix");

    Bytes badSignature(
        fixture.archive.begin(), fixture.archive.begin() + 30);
    PatchU32(badSignature, 0, 0);
    RequireError(
        kisak::iwd::RequiredLocalHeaderBytes(badSignature, required),
        Error::LocalHeaderSignature,
        "local signature mutation");

    RequireError(
        kisak::iwd::RequiredLocalHeaderBytes(
            std::span<const uint8_t>(fixture.archive).first(30), required),
        Error::None,
        "valid local prefix");
    Bytes complete(
        fixture.archive.begin(),
        fixture.archive.begin() + static_cast<Bytes::difference_type>(required));
    PatchU32(complete, 14, entry.crc32 ^ 1u);
    MemberLocation location;
    RequireError(
        kisak::iwd::ValidateLocalHeader(entry, locator, complete, location),
        Error::LocalHeaderMismatch,
        "local CRC mismatch");

    EntrySpec malformedExtra = Stored("extra.txt", "x");
    malformedExtra.localExtra = {0x34, 0x12, 0x02, 0x00, 0x00};
    const Fixture malformed = BuildFixture({malformedExtra});
    const auto malformedLocator = Locate(malformed);
    const auto malformedIndex = Parse(malformed, malformedLocator);
    const Entry &malformedEntry = malformedIndex.Entries()[0];
    const auto prefix = std::span<const uint8_t>(malformed.archive).first(30);
    RequireError(kisak::iwd::RequiredLocalHeaderBytes(prefix, required), Error::None,
        "malformed-extra local size");
    RequireError(
        kisak::iwd::ValidateLocalHeader(
            malformedEntry,
            malformedLocator,
            std::span<const uint8_t>(malformed.archive).first(required),
            location),
        Error::InvalidExtraField,
        "malformed local extra");
}

void TestDecoderFailures()
{
    EntrySpec crcFailure = Deflated("crc.txt", "crc payload");
    crcFailure.declaredCrc = Crc(crcFailure.contents) ^ 1u;
    const Fixture crcFixture = BuildFixture({crcFailure});
    const auto crcLocator = Locate(crcFixture);
    const auto crcIndex = Parse(crcFixture, crcLocator);
    const Entry &crcEntry = crcIndex.Entries()[0];
    const auto crcLocation = ValidateMember(crcFixture, crcLocator, crcEntry);
    const DecodeResult crcDecoded = Decode(crcFixture, crcEntry, crcLocation);
    RequireError(crcDecoded.consumeError, Error::None, "CRC fixture consume");
    RequireError(crcDecoded.finishError, Error::CrcMismatch, "CRC fixture finish");

    const Bytes original = ToBytes("truncated deflate payload truncated deflate payload");
    Bytes truncatedStream = DeflateRaw(original);
    Require(truncatedStream.size() > 1, "deflate fixture has truncatable bytes");
    truncatedStream.pop_back();
    EntrySpec truncated = Deflated("truncated.txt", "");
    truncated.contents = original;
    truncated.compressedData = truncatedStream;
    const Fixture truncatedFixture = BuildFixture({truncated});
    const auto truncatedLocator = Locate(truncatedFixture);
    const auto truncatedIndex = Parse(truncatedFixture, truncatedLocator);
    const Entry &truncatedEntry = truncatedIndex.Entries()[0];
    const auto truncatedLocation = ValidateMember(
        truncatedFixture, truncatedLocator, truncatedEntry);
    const DecodeResult truncatedDecoded = Decode(
        truncatedFixture, truncatedEntry, truncatedLocation);
    RequireError(truncatedDecoded.consumeError, Error::None, "truncated deflate consume");
    RequireError(
        truncatedDecoded.finishError,
        Error::DecompressionNotFinished,
        "truncated deflate finish");

    const Fixture limitFixture = BuildFixture({Deflated("limit.txt", "123456")});
    const auto limitLocator = Locate(limitFixture);
    const auto limitIndex = Parse(limitFixture, limitLocator);
    MemberDecoder limited;
    RequireError(
        limited.Begin(limitIndex.Entries()[0], 5),
        Error::DecoderOutputLimit,
        "decoder output limit");

    MemberDecoder premature;
    RequireError(premature.Begin(limitIndex.Entries()[0]), Error::None, "premature decoder begin");
    RequireError(premature.Finish(), Error::DecoderInputSizeMismatch, "premature decoder finish");

    const Fixture storedFixture = BuildFixture({Stored("stored.txt", "abc")});
    const auto storedLocator = Locate(storedFixture);
    const auto storedIndex = Parse(storedFixture, storedLocator);
    const Entry &storedEntry = storedIndex.Entries()[0];
    MemberDecoder storedDecoder;
    RequireError(storedDecoder.Begin(storedEntry), Error::None, "stored decoder begin");
    const std::array<uint8_t, 4> tooMuchInput = {'a', 'b', 'c', 'd'};
    std::array<uint8_t, 4> output{};
    std::size_t consumed = 0;
    std::size_t produced = 0;
    RequireError(
        storedDecoder.Consume(tooMuchInput, output, consumed, produced),
        Error::DecoderInputSizeMismatch,
        "decoder rejects excess compressed input");
}

void TestDefaultLimitValuesAndErrors()
{
    const Limits limits;
    Require(limits.maxEntries == 4096, "default entry limit");
    Require(limits.maxCentralDirectoryBytes == 512u * 1024u, "default central limit");
    Require(limits.maxPathBytes == 255, "default path limit");
    Require(limits.maxCumulativePathBytes == 1024u * 1024u, "default cumulative path limit");
    Require(limits.maxMemberCompressedBytes == 32u * 1024u * 1024u,
        "default compressed member limit");
    Require(limits.maxMemberUncompressedBytes == 32u * 1024u * 1024u,
        "default uncompressed member limit");
    Require(limits.maxTotalUncompressedBytes == 512ull * 1024ull * 1024ull,
        "default total output limit");
    Require(std::strlen(kisak::iwd::ErrorString(Error::CaseCollision)) > 0,
        "errors have printable descriptions");
}

class Runner
{
public:
    void Run(const char *name, const std::function<void()> &test)
    {
        try
        {
            test();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        }
        catch (const std::exception &error)
        {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
        catch (...)
        {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": unknown exception\n";
        }
    }

    int Result() const
    {
        std::cout << passed_ << " passed, " << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

private:
    int passed_ = 0;
    int failed_ = 0;
};
} // namespace

int main()
{
    Runner runner;
#if defined(KISAK_TEST_CANONICAL_MINIZIP)
    runner.Run("canonical minizip read/seek", TestCanonicalMinizipReadAndSeek);
#endif
    runner.Run("stored/deflated happy path", TestHappyPath);
    runner.Run("EOCD locator rejections", TestLocatorRejections);
    runner.Run("central metadata rejections", TestCentralMetadataRejections);
    runner.Run("path safety table", TestPathSafetyTable);
    runner.Run("collision and duplicate policy", TestCollisionPolicy);
    runner.Run("resource budgets", TestBudgets);
    runner.Run("central framing and atomicity", TestCentralFramingAndAtomicity);
    runner.Run("local-header validation", TestLocalHeaderValidation);
    runner.Run("decoder failures", TestDecoderFailures);
    runner.Run("default limits and error strings", TestDefaultLimitValuesAndErrors);
    return runner.Result();
}
