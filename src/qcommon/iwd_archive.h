#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kisak::iwd
{

// ZIP/IWD input is untrusted. These limits are applied while walking every
// central-directory record, including records later replaced by an exact-name
// duplicate.
struct Limits
{
    uint32_t maxEntries = 4096;
    uint32_t maxCentralDirectoryBytes = 512u * 1024u;
    uint32_t maxPathBytes = 255;
    uint32_t maxCumulativePathBytes = 1024u * 1024u;
    uint32_t maxMemberCompressedBytes = 32u * 1024u * 1024u;
    uint32_t maxMemberUncompressedBytes = 32u * 1024u * 1024u;
    uint64_t maxTotalUncompressedBytes = 512ull * 1024ull * 1024ull;
};

enum class Error : uint32_t
{
    None = 0,
    InvalidArgument,
    TailRange,
    EocdNotFound,
    MultiDiskUnsupported,
    Zip64Unsupported,
    EmptyArchive,
    EntryCountLimit,
    CentralDirectoryLimit,
    CentralDirectoryRange,
    CentralDirectorySizeMismatch,
    CentralDirectorySignature,
    CentralDirectoryTruncated,
    CentralDirectoryTrailingData,
    UnsupportedVersion,
    EncryptedEntry,
    DataDescriptorUnsupported,
    UnsupportedFlags,
    UnsupportedCompression,
    InvalidExtraField,
    UnsafePath,
    PathTooLong,
    CumulativePathLimit,
    CaseCollision,
    MemberCompressedLimit,
    MemberUncompressedLimit,
    TotalUncompressedLimit,
    StoredSizeMismatch,
    EntryRange,
    LocalHeaderTruncated,
    LocalHeaderSignature,
    LocalHeaderMismatch,
    DirectoryEntry,
    DecoderNotStarted,
    DecoderAlreadyFinished,
    DecoderOutputLimit,
    DecoderInputSizeMismatch,
    DecoderOutputSizeMismatch,
    DecompressionInit,
    DecompressionData,
    DecompressionNotFinished,
    CrcMismatch,
};

const char *ErrorString(Error error) noexcept;

struct CentralDirectoryLocator
{
    uint32_t archiveSize = 0;
    uint32_t eocdOffset = 0;
    uint32_t centralOffset = 0;
    uint32_t centralSize = 0;
    uint16_t entryCount = 0;
    uint16_t commentSize = 0;
};

// tail must be a terminal slice of the archive no larger than the maximum
// ZIP32 EOCD plus comment (65,557 bytes). It may be smaller when the caller
// already knows that a shorter terminal window contains the complete EOCD.
Error LocateCentralDirectory(
    std::span<const uint8_t> tail,
    uint64_t tailOffset,
    uint64_t archiveSize,
    const Limits &limits,
    CentralDirectoryLocator &locator) noexcept;

struct Entry
{
    std::string path;
    uint32_t crc32 = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint32_t localHeaderOffset = 0;
    uint32_t centralRecord = 0;
    uint16_t versionNeeded = 0;
    uint16_t flags = 0;
    uint16_t compressionMethod = 0;
    uint16_t modificationTime = 0;
    uint16_t modificationDate = 0;
    bool directory = false;
};

class ArchiveIndex
{
public:
    std::span<const Entry> Entries() const noexcept;

    // IWD lookup is ASCII case-insensitive, matching the portable portion of
    // the engine's historical lookup behavior. Parse rejects two distinct
    // stored names that would alias under this fold.
    const Entry *Find(std::string_view path) const;
    const Entry *FindExact(std::string_view path) const;

    uint32_t RecordCount() const noexcept;
    uint64_t TotalDeclaredUncompressedBytes() const noexcept;

private:
    friend Error ParseCentralDirectory(
        std::span<const uint8_t>,
        const CentralDirectoryLocator &,
        const Limits &,
        ArchiveIndex &);

    std::vector<Entry> entries_;
    std::unordered_map<std::string, std::size_t> exactToSlot_;
    std::unordered_map<std::string, std::size_t> foldedToSlot_;
    uint32_t recordCount_ = 0;
    uint64_t totalDeclaredUncompressedBytes_ = 0;
};

// centralDirectory must contain exactly locator.centralSize bytes. Successful
// parsing replaces index atomically. Exact duplicate paths use the metadata
// from their final central record; case-only aliases are rejected.
Error ParseCentralDirectory(
    std::span<const uint8_t> centralDirectory,
    const CentralDirectoryLocator &locator,
    const Limits &limits,
    ArchiveIndex &index);

struct MemberLocation
{
    uint32_t dataOffset = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
};

// Call this after reading the fixed 30-byte local-header prefix. The returned
// size is the exact number of local-header bytes ValidateLocalHeader expects.
Error RequiredLocalHeaderBytes(
    std::span<const uint8_t> prefix,
    uint32_t &requiredBytes) noexcept;

// localHeader must begin at entry.localHeaderOffset and contain exactly the
// number of bytes returned by RequiredLocalHeaderBytes. The caller retains all
// I/O ownership; this function only validates metadata and returns the bounded
// compressed-data range to request next.
Error ValidateLocalHeader(
    const Entry &entry,
    const CentralDirectoryLocator &locator,
    std::span<const uint8_t> localHeader,
    MemberLocation &location) noexcept;

enum class DecoderProgress : uint8_t
{
    NotStarted,
    NeedsInput,
    NeedsOutput,
    ReadyToFinish,
    Finished,
};

// Incremental stored/raw-deflate decoder. Consume may accept only part of an
// input span when the supplied output span fills; inputConsumed tells the
// caller exactly which suffix must be presented again. Finish requires exact
// compressed and uncompressed byte counts, a complete deflate stream, and the
// central-directory CRC.
class MemberDecoder
{
public:
    MemberDecoder();
    ~MemberDecoder();
    MemberDecoder(MemberDecoder &&) noexcept;
    MemberDecoder &operator=(MemberDecoder &&) noexcept;
    MemberDecoder(const MemberDecoder &) = delete;
    MemberDecoder &operator=(const MemberDecoder &) = delete;

    Error Begin(
        const Entry &entry,
        uint32_t outputLimit = 32u * 1024u * 1024u) noexcept;

    Error Consume(
        std::span<const uint8_t> input,
        std::span<uint8_t> output,
        std::size_t &inputConsumed,
        std::size_t &outputProduced) noexcept;

    Error Finish() noexcept;

    DecoderProgress Progress() const noexcept;
    uint32_t CompressedBytesConsumed() const noexcept;
    uint32_t UncompressedBytesProduced() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kisak::iwd
