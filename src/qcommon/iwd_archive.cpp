#include <qcommon/iwd_archive.h>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace kisak::iwd
{
namespace
{
constexpr uint32_t ZIP_LOCAL_SIGNATURE = 0x04034b50u;
constexpr uint32_t ZIP_CENTRAL_SIGNATURE = 0x02014b50u;
constexpr uint32_t ZIP_EOCD_SIGNATURE = 0x06054b50u;
constexpr uint32_t ZIP64_LOCATOR_SIGNATURE = 0x07064b50u;
constexpr uint16_t ZIP64_EXTRA_ID = 0x0001u;

constexpr std::size_t ZIP_LOCAL_HEADER_SIZE = 30;
constexpr std::size_t ZIP_CENTRAL_HEADER_SIZE = 46;
constexpr std::size_t ZIP_EOCD_SIZE = 22;
constexpr std::size_t ZIP_MAX_COMMENT_SIZE = 0xffff;
constexpr std::size_t ZIP_MAX_TAIL_SIZE = ZIP_EOCD_SIZE + ZIP_MAX_COMMENT_SIZE;

constexpr uint16_t FLAG_ENCRYPTED = 0x0001u;
constexpr uint16_t FLAG_DEFLATE_OPTIONS = 0x0006u;
constexpr uint16_t FLAG_DATA_DESCRIPTOR = 0x0008u;
constexpr uint16_t FLAG_STRONG_ENCRYPTION = 0x0040u;
constexpr uint16_t FLAG_UTF8 = 0x0800u;
constexpr uint16_t FLAG_MASKED_HEADER = 0x2000u;

bool ReadU16(std::span<const uint8_t> bytes, std::size_t offset, uint16_t &value) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < 2)
    {
        return false;
    }
    value = static_cast<uint16_t>(bytes[offset]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
    return true;
}

bool ReadU32(std::span<const uint8_t> bytes, std::size_t offset, uint32_t &value) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < 4)
    {
        return false;
    }
    value = static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    return true;
}

bool IsAsciiAlpha(uint8_t byte) noexcept
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
}

char FoldAscii(char character) noexcept
{
    const auto byte = static_cast<uint8_t>(character);
    if (byte >= 'A' && byte <= 'Z')
    {
        return static_cast<char>(byte + ('a' - 'A'));
    }
    return character;
}

std::string FoldPath(std::string_view path)
{
    std::string folded(path);
    std::transform(folded.begin(), folded.end(), folded.begin(), FoldAscii);
    return folded;
}

bool IsValidUtf8(std::span<const uint8_t> bytes) noexcept
{
    std::size_t index = 0;
    while (index < bytes.size())
    {
        const uint8_t lead = bytes[index];
        if (lead < 0x80)
        {
            ++index;
            continue;
        }

        std::size_t continuationCount = 0;
        uint32_t codePoint = 0;
        uint32_t minimumCodePoint = 0;
        if ((lead & 0xe0u) == 0xc0u)
        {
            continuationCount = 1;
            codePoint = lead & 0x1fu;
            minimumCodePoint = 0x80;
        }
        else if ((lead & 0xf0u) == 0xe0u)
        {
            continuationCount = 2;
            codePoint = lead & 0x0fu;
            minimumCodePoint = 0x800;
        }
        else if ((lead & 0xf8u) == 0xf0u)
        {
            continuationCount = 3;
            codePoint = lead & 0x07u;
            minimumCodePoint = 0x10000;
        }
        else
        {
            return false;
        }

        if (continuationCount > bytes.size() - index - 1)
        {
            return false;
        }
        for (std::size_t continuation = 1; continuation <= continuationCount; ++continuation)
        {
            const uint8_t next = bytes[index + continuation];
            if ((next & 0xc0u) != 0x80u)
            {
                return false;
            }
            codePoint = (codePoint << 6) | (next & 0x3fu);
        }
        if (codePoint < minimumCodePoint || codePoint > 0x10ffffu ||
            (codePoint >= 0x80u && codePoint <= 0x9fu) ||
            (codePoint >= 0xd800u && codePoint <= 0xdfffu))
        {
            return false;
        }
        index += continuationCount + 1;
    }
    return true;
}

Error ValidateFlags(uint16_t flags, uint16_t compressionMethod) noexcept
{
    if ((flags & (FLAG_ENCRYPTED | FLAG_STRONG_ENCRYPTION | FLAG_MASKED_HEADER)) != 0)
    {
        return Error::EncryptedEntry;
    }
    if ((flags & FLAG_DATA_DESCRIPTOR) != 0)
    {
        return Error::DataDescriptorUnsupported;
    }

    const uint16_t allowed = compressionMethod == 8
        ? static_cast<uint16_t>(FLAG_DEFLATE_OPTIONS | FLAG_UTF8)
        : FLAG_UTF8;
    return (flags & static_cast<uint16_t>(~allowed)) == 0
        ? Error::None
        : Error::UnsupportedFlags;
}

Error ValidateExtraFields(std::span<const uint8_t> extras) noexcept
{
    std::size_t cursor = 0;
    while (cursor < extras.size())
    {
        uint16_t identifier = 0;
        uint16_t dataSize = 0;
        if (!ReadU16(extras, cursor, identifier) || !ReadU16(extras, cursor + 2, dataSize))
        {
            return Error::InvalidExtraField;
        }
        cursor += 4;
        if (dataSize > extras.size() - cursor)
        {
            return Error::InvalidExtraField;
        }
        if (identifier == ZIP64_EXTRA_ID)
        {
            return Error::Zip64Unsupported;
        }
        cursor += dataSize;
    }
    return Error::None;
}

Error ValidatePath(std::span<const uint8_t> path, uint16_t flags) noexcept
{
    if (path.empty() || path.front() == '/')
    {
        return Error::UnsafePath;
    }
    if (path.size() >= 2 && IsAsciiAlpha(path[0]) && path[1] == ':')
    {
        return Error::UnsafePath;
    }
    if ((flags & FLAG_UTF8) != 0 && !IsValidUtf8(path))
    {
        return Error::UnsafePath;
    }

    for (uint8_t byte : path)
    {
        // Colon is rejected everywhere, not only in a leading drive prefix:
        // the historical engine comparison treats it as a path separator and
        // would otherwise give one stored name two lookup interpretations.
        if (byte == '\\' || byte == ':' || byte == 0 || byte < 0x20u || byte == 0x7fu)
        {
            return Error::UnsafePath;
        }
    }

    std::size_t segmentStart = 0;
    for (std::size_t index = 0; index <= path.size(); ++index)
    {
        if (index != path.size() && path[index] != '/')
        {
            continue;
        }

        const std::size_t segmentLength = index - segmentStart;
        const bool terminalDirectorySeparator =
            index == path.size() && segmentLength == 0 && index > 0;
        if (segmentLength == 0 && !terminalDirectorySeparator)
        {
            return Error::UnsafePath;
        }
        if (segmentLength == 1 && path[segmentStart] == '.')
        {
            return Error::UnsafePath;
        }
        if (segmentLength == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.')
        {
            return Error::UnsafePath;
        }
        segmentStart = index + 1;
    }
    return Error::None;
}

Error ValidateVersion(uint16_t versionNeeded) noexcept
{
    // Stored and deflated ZIP32 members require at most PKZIP 2.0. In
    // particular, version 4.5 is the ZIP64 marker even if sentinel fields were
    // maliciously omitted.
    if (versionNeeded >= 45)
    {
        return Error::Zip64Unsupported;
    }
    return versionNeeded <= 20 ? Error::None : Error::UnsupportedVersion;
}

Error CheckedRecordEnd(
    std::size_t cursor,
    uint16_t nameSize,
    uint16_t extraSize,
    uint16_t commentSize,
    std::size_t bufferSize,
    std::size_t &recordEnd) noexcept
{
    const uint64_t end = static_cast<uint64_t>(cursor) + ZIP_CENTRAL_HEADER_SIZE +
        nameSize + extraSize + commentSize;
    if (end > bufferSize)
    {
        return Error::CentralDirectoryTruncated;
    }
    recordEnd = static_cast<std::size_t>(end);
    return Error::None;
}

void EndInflate(z_stream &stream, bool &initialized) noexcept
{
    if (initialized)
    {
        inflateEnd(&stream);
        initialized = false;
    }
}
} // namespace

const char *ErrorString(Error error) noexcept
{
    switch (error)
    {
    case Error::None: return "success";
    case Error::InvalidArgument: return "invalid archive-reader argument";
    case Error::TailRange: return "EOCD tail is not a bounded terminal archive slice";
    case Error::EocdNotFound: return "ZIP end-of-central-directory record was not found";
    case Error::MultiDiskUnsupported: return "multi-disk ZIP archives are unsupported";
    case Error::Zip64Unsupported: return "ZIP64 archives are unsupported";
    case Error::EmptyArchive: return "archive contains no central-directory entries";
    case Error::EntryCountLimit: return "archive entry-count limit exceeded";
    case Error::CentralDirectoryLimit: return "central-directory byte limit exceeded";
    case Error::CentralDirectoryRange: return "central-directory range is invalid";
    case Error::CentralDirectorySizeMismatch: return "central-directory buffer size does not match its locator";
    case Error::CentralDirectorySignature: return "invalid central-directory entry signature";
    case Error::CentralDirectoryTruncated: return "central-directory entry is truncated";
    case Error::CentralDirectoryTrailingData: return "central directory contains unaccounted trailing data";
    case Error::UnsupportedVersion: return "ZIP entry requires an unsupported extractor version";
    case Error::EncryptedEntry: return "encrypted ZIP entries are unsupported";
    case Error::DataDescriptorUnsupported: return "ZIP data descriptors are unsupported";
    case Error::UnsupportedFlags: return "ZIP entry uses unsupported general-purpose flags";
    case Error::UnsupportedCompression: return "ZIP entry uses an unsupported compression method";
    case Error::InvalidExtraField: return "ZIP extra-field framing is malformed";
    case Error::UnsafePath: return "ZIP entry path is unsafe or ambiguous";
    case Error::PathTooLong: return "ZIP entry path is too long";
    case Error::CumulativePathLimit: return "cumulative ZIP path-byte limit exceeded";
    case Error::CaseCollision: return "distinct ZIP paths collide under case-insensitive lookup";
    case Error::MemberCompressedLimit: return "ZIP member compressed-size limit exceeded";
    case Error::MemberUncompressedLimit: return "ZIP member uncompressed-size limit exceeded";
    case Error::TotalUncompressedLimit: return "archive declared-output limit exceeded";
    case Error::StoredSizeMismatch: return "stored ZIP member sizes do not match";
    case Error::EntryRange: return "ZIP member range overlaps or escapes the archive data region";
    case Error::LocalHeaderTruncated: return "ZIP local header is truncated";
    case Error::LocalHeaderSignature: return "invalid ZIP local-header signature";
    case Error::LocalHeaderMismatch: return "ZIP local and central metadata do not match";
    case Error::DirectoryEntry: return "ZIP directory entries cannot be decoded as members";
    case Error::DecoderNotStarted: return "member decoder has not been started";
    case Error::DecoderAlreadyFinished: return "member decoder is already finished";
    case Error::DecoderOutputLimit: return "member decoder output limit exceeded";
    case Error::DecoderInputSizeMismatch: return "member compressed input size does not match its declaration";
    case Error::DecoderOutputSizeMismatch: return "member output size does not match its declaration";
    case Error::DecompressionInit: return "raw-deflate decoder initialization failed";
    case Error::DecompressionData: return "raw-deflate stream is malformed";
    case Error::DecompressionNotFinished: return "raw-deflate stream did not finish at the declared boundary";
    case Error::CrcMismatch: return "decoded member CRC-32 does not match its declaration";
    }
    return "unknown archive-reader error";
}

Error LocateCentralDirectory(
    std::span<const uint8_t> tail,
    uint64_t tailOffset,
    uint64_t archiveSize,
    const Limits &limits,
    CentralDirectoryLocator &locator) noexcept
{
    locator = {};
    if (archiveSize > std::numeric_limits<uint32_t>::max())
    {
        return Error::Zip64Unsupported;
    }
    if (tailOffset > archiveSize || tail.size() > ZIP_MAX_TAIL_SIZE ||
        tail.size() != archiveSize - tailOffset)
    {
        return Error::TailRange;
    }
    if (tail.size() < ZIP_EOCD_SIZE)
    {
        return Error::EocdNotFound;
    }

    std::size_t eocdPosition = tail.size();
    for (std::size_t position = tail.size() - ZIP_EOCD_SIZE;; --position)
    {
        uint32_t signature = 0;
        uint16_t commentSize = 0;
        if (ReadU32(tail, position, signature) && signature == ZIP_EOCD_SIGNATURE &&
            ReadU16(tail, position + 20, commentSize) &&
            position + ZIP_EOCD_SIZE + commentSize == tail.size())
        {
            eocdPosition = position;
            break;
        }
        if (position == 0)
        {
            break;
        }
    }
    if (eocdPosition == tail.size())
    {
        return Error::EocdNotFound;
    }

    uint16_t diskNumber = 0;
    uint16_t centralDisk = 0;
    uint16_t entriesOnDisk = 0;
    uint16_t entryCount = 0;
    uint16_t commentSize = 0;
    uint32_t centralSize = 0;
    uint32_t centralOffset = 0;
    if (!ReadU16(tail, eocdPosition + 4, diskNumber) ||
        !ReadU16(tail, eocdPosition + 6, centralDisk) ||
        !ReadU16(tail, eocdPosition + 8, entriesOnDisk) ||
        !ReadU16(tail, eocdPosition + 10, entryCount) ||
        !ReadU32(tail, eocdPosition + 12, centralSize) ||
        !ReadU32(tail, eocdPosition + 16, centralOffset) ||
        !ReadU16(tail, eocdPosition + 20, commentSize))
    {
        return Error::EocdNotFound;
    }

    if (diskNumber == 0xffffu || centralDisk == 0xffffu ||
        entriesOnDisk == 0xffffu || entryCount == 0xffffu ||
        centralSize == 0xffffffffu || centralOffset == 0xffffffffu)
    {
        return Error::Zip64Unsupported;
    }
    if (diskNumber != 0 || centralDisk != 0 || entriesOnDisk != entryCount)
    {
        return Error::MultiDiskUnsupported;
    }
    if (entryCount == 0)
    {
        return Error::EmptyArchive;
    }
    if (entryCount > limits.maxEntries)
    {
        return Error::EntryCountLimit;
    }
    if (centralSize > limits.maxCentralDirectoryBytes)
    {
        return Error::CentralDirectoryLimit;
    }
    if (centralSize < static_cast<uint64_t>(entryCount) * ZIP_CENTRAL_HEADER_SIZE)
    {
        return Error::CentralDirectoryRange;
    }

    const uint64_t absoluteEocd = tailOffset + eocdPosition;
    if (centralOffset > absoluteEocd ||
        static_cast<uint64_t>(centralOffset) + centralSize != absoluteEocd)
    {
        return Error::CentralDirectoryRange;
    }

    if (eocdPosition >= 20)
    {
        uint32_t precedingSignature = 0;
        if (ReadU32(tail, eocdPosition - 20, precedingSignature) &&
            precedingSignature == ZIP64_LOCATOR_SIGNATURE)
        {
            return Error::Zip64Unsupported;
        }
    }

    locator.archiveSize = static_cast<uint32_t>(archiveSize);
    locator.eocdOffset = static_cast<uint32_t>(absoluteEocd);
    locator.centralOffset = centralOffset;
    locator.centralSize = centralSize;
    locator.entryCount = entryCount;
    locator.commentSize = commentSize;
    return Error::None;
}

std::span<const Entry> ArchiveIndex::Entries() const noexcept
{
    return entries_;
}

const Entry *ArchiveIndex::Find(std::string_view path) const
{
    const auto found = foldedToSlot_.find(FoldPath(path));
    return found == foldedToSlot_.end() ? nullptr : &entries_[found->second];
}

const Entry *ArchiveIndex::FindExact(std::string_view path) const
{
    const auto found = exactToSlot_.find(std::string(path));
    return found == exactToSlot_.end() ? nullptr : &entries_[found->second];
}

uint32_t ArchiveIndex::RecordCount() const noexcept
{
    return recordCount_;
}

uint64_t ArchiveIndex::TotalDeclaredUncompressedBytes() const noexcept
{
    return totalDeclaredUncompressedBytes_;
}

Error ParseCentralDirectory(
    std::span<const uint8_t> centralDirectory,
    const CentralDirectoryLocator &locator,
    const Limits &limits,
    ArchiveIndex &index)
{
    if (locator.entryCount == 0 || locator.archiveSize == 0)
    {
        return Error::InvalidArgument;
    }
    if (locator.entryCount > limits.maxEntries)
    {
        return Error::EntryCountLimit;
    }
    if (locator.centralSize > limits.maxCentralDirectoryBytes)
    {
        return Error::CentralDirectoryLimit;
    }
    if (centralDirectory.size() != locator.centralSize)
    {
        return Error::CentralDirectorySizeMismatch;
    }
    if (static_cast<uint64_t>(locator.centralOffset) + locator.centralSize != locator.eocdOffset ||
        locator.eocdOffset > locator.archiveSize)
    {
        return Error::CentralDirectoryRange;
    }

    ArchiveIndex parsed;
    parsed.entries_.reserve(locator.entryCount);
    parsed.exactToSlot_.reserve(locator.entryCount);
    parsed.foldedToSlot_.reserve(locator.entryCount);

    std::size_t cursor = 0;
    uint64_t cumulativePathBytes = 0;
    uint64_t totalUncompressedBytes = 0;
    for (uint32_t record = 0; record < locator.entryCount; ++record)
    {
        if (cursor > centralDirectory.size() ||
            centralDirectory.size() - cursor < ZIP_CENTRAL_HEADER_SIZE)
        {
            return Error::CentralDirectoryTruncated;
        }

        uint32_t signature = 0;
        if (!ReadU32(centralDirectory, cursor, signature) || signature != ZIP_CENTRAL_SIGNATURE)
        {
            return Error::CentralDirectorySignature;
        }

        Entry entry;
        uint16_t nameSize = 0;
        uint16_t extraSize = 0;
        uint16_t commentSize = 0;
        uint16_t diskStart = 0;
        if (!ReadU16(centralDirectory, cursor + 6, entry.versionNeeded) ||
            !ReadU16(centralDirectory, cursor + 8, entry.flags) ||
            !ReadU16(centralDirectory, cursor + 10, entry.compressionMethod) ||
            !ReadU16(centralDirectory, cursor + 12, entry.modificationTime) ||
            !ReadU16(centralDirectory, cursor + 14, entry.modificationDate) ||
            !ReadU32(centralDirectory, cursor + 16, entry.crc32) ||
            !ReadU32(centralDirectory, cursor + 20, entry.compressedSize) ||
            !ReadU32(centralDirectory, cursor + 24, entry.uncompressedSize) ||
            !ReadU16(centralDirectory, cursor + 28, nameSize) ||
            !ReadU16(centralDirectory, cursor + 30, extraSize) ||
            !ReadU16(centralDirectory, cursor + 32, commentSize) ||
            !ReadU16(centralDirectory, cursor + 34, diskStart) ||
            !ReadU32(centralDirectory, cursor + 42, entry.localHeaderOffset))
        {
            return Error::CentralDirectoryTruncated;
        }

        std::size_t recordEnd = 0;
        if (const Error endError = CheckedRecordEnd(
                cursor, nameSize, extraSize, commentSize, centralDirectory.size(), recordEnd);
            endError != Error::None)
        {
            return endError;
        }

        if (diskStart == 0xffffu || entry.compressedSize == 0xffffffffu ||
            entry.uncompressedSize == 0xffffffffu || entry.localHeaderOffset == 0xffffffffu)
        {
            return Error::Zip64Unsupported;
        }
        if (diskStart != 0)
        {
            return Error::MultiDiskUnsupported;
        }
        if (const Error versionError = ValidateVersion(entry.versionNeeded);
            versionError != Error::None)
        {
            return versionError;
        }
        if (entry.compressionMethod != 0 && entry.compressionMethod != 8)
        {
            return Error::UnsupportedCompression;
        }
        if (const Error flagError = ValidateFlags(entry.flags, entry.compressionMethod);
            flagError != Error::None)
        {
            return flagError;
        }
        if (nameSize == 0)
        {
            return Error::UnsafePath;
        }
        if (nameSize > limits.maxPathBytes)
        {
            return Error::PathTooLong;
        }

        const std::size_t nameOffset = cursor + ZIP_CENTRAL_HEADER_SIZE;
        const std::span<const uint8_t> pathBytes = centralDirectory.subspan(nameOffset, nameSize);
        if (const Error pathError = ValidatePath(pathBytes, entry.flags); pathError != Error::None)
        {
            return pathError;
        }
        const std::span<const uint8_t> extras =
            centralDirectory.subspan(nameOffset + nameSize, extraSize);
        if (const Error extraError = ValidateExtraFields(extras); extraError != Error::None)
        {
            return extraError;
        }

        cumulativePathBytes += nameSize;
        if (cumulativePathBytes > limits.maxCumulativePathBytes)
        {
            return Error::CumulativePathLimit;
        }
        if (entry.compressedSize > limits.maxMemberCompressedBytes)
        {
            return Error::MemberCompressedLimit;
        }
        if (entry.uncompressedSize > limits.maxMemberUncompressedBytes)
        {
            return Error::MemberUncompressedLimit;
        }
        totalUncompressedBytes += entry.uncompressedSize;
        if (totalUncompressedBytes > limits.maxTotalUncompressedBytes)
        {
            return Error::TotalUncompressedLimit;
        }
        if (entry.compressionMethod == 0 && entry.compressedSize != entry.uncompressedSize)
        {
            return Error::StoredSizeMismatch;
        }

        entry.path.assign(reinterpret_cast<const char *>(pathBytes.data()), pathBytes.size());
        entry.directory = entry.path.back() == '/';
        if (entry.directory && (entry.compressedSize != 0 || entry.uncompressedSize != 0))
        {
            return Error::StoredSizeMismatch;
        }
        if (entry.localHeaderOffset >= locator.centralOffset ||
            static_cast<uint64_t>(entry.localHeaderOffset) + ZIP_LOCAL_HEADER_SIZE +
                entry.compressedSize > locator.centralOffset)
        {
            return Error::EntryRange;
        }
        entry.centralRecord = record;

        const std::string folded = FoldPath(entry.path);
        const auto foldedFound = parsed.foldedToSlot_.find(folded);
        const auto exactFound = parsed.exactToSlot_.find(entry.path);
        if (foldedFound != parsed.foldedToSlot_.end() && exactFound == parsed.exactToSlot_.end())
        {
            return Error::CaseCollision;
        }

        if (exactFound != parsed.exactToSlot_.end())
        {
            parsed.entries_[exactFound->second] = std::move(entry);
        }
        else
        {
            const std::size_t slot = parsed.entries_.size();
            parsed.entries_.push_back(std::move(entry));
            parsed.exactToSlot_.emplace(parsed.entries_.back().path, slot);
            parsed.foldedToSlot_.emplace(folded, slot);
        }
        cursor = recordEnd;
    }

    if (cursor != centralDirectory.size())
    {
        return Error::CentralDirectoryTrailingData;
    }

    parsed.recordCount_ = locator.entryCount;
    parsed.totalDeclaredUncompressedBytes_ = totalUncompressedBytes;
    index = std::move(parsed);
    return Error::None;
}

Error RequiredLocalHeaderBytes(
    std::span<const uint8_t> prefix,
    uint32_t &requiredBytes) noexcept
{
    requiredBytes = 0;
    if (prefix.size() < ZIP_LOCAL_HEADER_SIZE)
    {
        return Error::LocalHeaderTruncated;
    }
    uint32_t signature = 0;
    uint16_t nameSize = 0;
    uint16_t extraSize = 0;
    if (!ReadU32(prefix, 0, signature) || signature != ZIP_LOCAL_SIGNATURE)
    {
        return Error::LocalHeaderSignature;
    }
    if (!ReadU16(prefix, 26, nameSize) || !ReadU16(prefix, 28, extraSize))
    {
        return Error::LocalHeaderTruncated;
    }
    requiredBytes = static_cast<uint32_t>(ZIP_LOCAL_HEADER_SIZE) + nameSize + extraSize;
    return Error::None;
}

Error ValidateLocalHeader(
    const Entry &entry,
    const CentralDirectoryLocator &locator,
    std::span<const uint8_t> localHeader,
    MemberLocation &location) noexcept
{
    location = {};
    uint32_t requiredBytes = 0;
    const Error sizeError = RequiredLocalHeaderBytes(localHeader, requiredBytes);
    if (sizeError != Error::None)
    {
        return sizeError;
    }
    if (localHeader.size() != requiredBytes)
    {
        return localHeader.size() < requiredBytes
            ? Error::LocalHeaderTruncated
            : Error::LocalHeaderMismatch;
    }

    uint16_t versionNeeded = 0;
    uint16_t flags = 0;
    uint16_t compressionMethod = 0;
    uint16_t modificationTime = 0;
    uint16_t modificationDate = 0;
    uint16_t nameSize = 0;
    uint16_t extraSize = 0;
    uint32_t crc = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    if (!ReadU16(localHeader, 4, versionNeeded) ||
        !ReadU16(localHeader, 6, flags) ||
        !ReadU16(localHeader, 8, compressionMethod) ||
        !ReadU16(localHeader, 10, modificationTime) ||
        !ReadU16(localHeader, 12, modificationDate) ||
        !ReadU32(localHeader, 14, crc) ||
        !ReadU32(localHeader, 18, compressedSize) ||
        !ReadU32(localHeader, 22, uncompressedSize) ||
        !ReadU16(localHeader, 26, nameSize) ||
        !ReadU16(localHeader, 28, extraSize))
    {
        return Error::LocalHeaderTruncated;
    }
    if (const Error versionError = ValidateVersion(versionNeeded); versionError != Error::None)
    {
        return versionError;
    }
    if (compressionMethod != 0 && compressionMethod != 8)
    {
        return Error::UnsupportedCompression;
    }
    if (const Error flagError = ValidateFlags(flags, compressionMethod); flagError != Error::None)
    {
        return flagError;
    }
    if (versionNeeded != entry.versionNeeded || flags != entry.flags ||
        compressionMethod != entry.compressionMethod ||
        modificationTime != entry.modificationTime || modificationDate != entry.modificationDate ||
        crc != entry.crc32 || compressedSize != entry.compressedSize ||
        uncompressedSize != entry.uncompressedSize || nameSize != entry.path.size())
    {
        return Error::LocalHeaderMismatch;
    }

    const std::span<const uint8_t> localPath = localHeader.subspan(ZIP_LOCAL_HEADER_SIZE, nameSize);
    if (!std::equal(localPath.begin(), localPath.end(),
            reinterpret_cast<const uint8_t *>(entry.path.data())))
    {
        return Error::LocalHeaderMismatch;
    }
    const std::span<const uint8_t> extras =
        localHeader.subspan(ZIP_LOCAL_HEADER_SIZE + nameSize, extraSize);
    if (const Error extraError = ValidateExtraFields(extras); extraError != Error::None)
    {
        return extraError;
    }

    const uint64_t dataOffset = static_cast<uint64_t>(entry.localHeaderOffset) + requiredBytes;
    if (entry.localHeaderOffset >= locator.centralOffset || dataOffset > locator.centralOffset ||
        entry.compressedSize > static_cast<uint64_t>(locator.centralOffset) - dataOffset ||
        locator.eocdOffset > locator.archiveSize)
    {
        return Error::EntryRange;
    }
    location.dataOffset = static_cast<uint32_t>(dataOffset);
    location.compressedSize = entry.compressedSize;
    location.uncompressedSize = entry.uncompressedSize;
    return Error::None;
}

struct MemberDecoder::Impl
{
    z_stream stream{};
    bool streamInitialized = false;
    bool streamEnded = false;
    bool started = false;
    bool finished = false;
    uint16_t method = 0;
    uint32_t expectedCrc = 0;
    uint32_t expectedCompressed = 0;
    uint32_t expectedUncompressed = 0;
    uint32_t compressedConsumed = 0;
    uint32_t uncompressedProduced = 0;
    uint32_t runningCrc = 0;
    DecoderProgress progress = DecoderProgress::NotStarted;

    ~Impl()
    {
        EndInflate(stream, streamInitialized);
    }

    void Reset() noexcept
    {
        EndInflate(stream, streamInitialized);
        stream = {};
        streamEnded = false;
        started = false;
        finished = false;
        method = 0;
        expectedCrc = 0;
        expectedCompressed = 0;
        expectedUncompressed = 0;
        compressedConsumed = 0;
        uncompressedProduced = 0;
        runningCrc = 0;
        progress = DecoderProgress::NotStarted;
    }

    void UpdateProgress(bool outputWasExhausted) noexcept
    {
        if (finished)
        {
            progress = DecoderProgress::Finished;
            return;
        }
        const bool byteCountsComplete = compressedConsumed == expectedCompressed &&
            uncompressedProduced == expectedUncompressed;
        if (byteCountsComplete && (method == 0 || streamEnded))
        {
            progress = DecoderProgress::ReadyToFinish;
        }
        else if (outputWasExhausted && uncompressedProduced < expectedUncompressed)
        {
            progress = DecoderProgress::NeedsOutput;
        }
        else
        {
            progress = DecoderProgress::NeedsInput;
        }
    }
};

MemberDecoder::MemberDecoder()
    : impl_(std::make_unique<Impl>())
{
}

MemberDecoder::~MemberDecoder()
{
    if (impl_)
    {
        impl_->Reset();
    }
}

MemberDecoder::MemberDecoder(MemberDecoder &&) noexcept = default;
MemberDecoder &MemberDecoder::operator=(MemberDecoder &&) noexcept = default;

Error MemberDecoder::Begin(const Entry &entry, uint32_t outputLimit) noexcept
{
    if (!impl_)
    {
        return Error::DecoderNotStarted;
    }
    impl_->Reset();
    if (entry.directory)
    {
        return Error::DirectoryEntry;
    }
    if (entry.compressionMethod != 0 && entry.compressionMethod != 8)
    {
        return Error::UnsupportedCompression;
    }
    if (entry.uncompressedSize > outputLimit)
    {
        return Error::DecoderOutputLimit;
    }
    if (entry.compressionMethod == 0 && entry.compressedSize != entry.uncompressedSize)
    {
        return Error::StoredSizeMismatch;
    }

    impl_->method = entry.compressionMethod;
    impl_->expectedCrc = entry.crc32;
    impl_->expectedCompressed = entry.compressedSize;
    impl_->expectedUncompressed = entry.uncompressedSize;
    impl_->runningCrc = static_cast<uint32_t>(crc32(0L, Z_NULL, 0));
    impl_->started = true;

    if (impl_->method == 8)
    {
        impl_->stream.zalloc = Z_NULL;
        impl_->stream.zfree = Z_NULL;
        impl_->stream.opaque = Z_NULL;
        if (inflateInit2(&impl_->stream, -MAX_WBITS) != Z_OK)
        {
            impl_->Reset();
            return Error::DecompressionInit;
        }
        impl_->streamInitialized = true;
    }
    impl_->UpdateProgress(false);
    return Error::None;
}

Error MemberDecoder::Consume(
    std::span<const uint8_t> input,
    std::span<uint8_t> output,
    std::size_t &inputConsumed,
    std::size_t &outputProduced) noexcept
{
    inputConsumed = 0;
    outputProduced = 0;
    if (!impl_ || !impl_->started)
    {
        return Error::DecoderNotStarted;
    }
    if (impl_->finished)
    {
        return Error::DecoderAlreadyFinished;
    }

    const uint32_t compressedRemaining =
        impl_->expectedCompressed - impl_->compressedConsumed;
    const uint32_t uncompressedRemaining =
        impl_->expectedUncompressed - impl_->uncompressedProduced;
    if (input.size() > compressedRemaining ||
        input.size() > std::numeric_limits<uInt>::max() ||
        output.size() > std::numeric_limits<uInt>::max())
    {
        return Error::DecoderInputSizeMismatch;
    }
    if (impl_->streamEnded && !input.empty())
    {
        return Error::DecoderInputSizeMismatch;
    }

    if (impl_->method == 0)
    {
        const std::size_t count = std::min(input.size(), output.size());
        if (count > uncompressedRemaining)
        {
            return Error::DecoderOutputSizeMismatch;
        }
        if (count > 0)
        {
            std::memcpy(output.data(), input.data(), count);
            impl_->runningCrc = static_cast<uint32_t>(crc32(
                impl_->runningCrc,
                reinterpret_cast<const Bytef *>(output.data()),
                static_cast<uInt>(count)));
            impl_->compressedConsumed += static_cast<uint32_t>(count);
            impl_->uncompressedProduced += static_cast<uint32_t>(count);
            inputConsumed = count;
            outputProduced = count;
        }
        impl_->UpdateProgress(!input.empty() && count < input.size());
        return Error::None;
    }

    if (uncompressedRemaining > 0 && output.empty())
    {
        impl_->UpdateProgress(true);
        return Error::None;
    }

    std::array<uint8_t, 1> overflowGuard{};
    uint8_t *outputData = output.data();
    uInt outputCapacity = static_cast<uInt>(
        std::min<std::size_t>(output.size(), uncompressedRemaining));
    const bool guardingOutputBoundary = uncompressedRemaining == 0;
    if (guardingOutputBoundary)
    {
        outputData = overflowGuard.data();
        outputCapacity = 1;
    }

    impl_->stream.next_in = input.empty()
        ? Z_NULL
        : const_cast<Bytef *>(reinterpret_cast<const Bytef *>(input.data()));
    impl_->stream.avail_in = static_cast<uInt>(input.size());
    impl_->stream.next_out = reinterpret_cast<Bytef *>(outputData);
    impl_->stream.avail_out = outputCapacity;

    const uInt inputBefore = impl_->stream.avail_in;
    const uInt outputBefore = impl_->stream.avail_out;
    const int inflateResult = inflate(&impl_->stream, Z_NO_FLUSH);
    const uInt consumed = inputBefore - impl_->stream.avail_in;
    const uInt produced = outputBefore - impl_->stream.avail_out;
    inputConsumed = consumed;
    impl_->compressedConsumed += consumed;
    if (guardingOutputBoundary && produced != 0)
    {
        return Error::DecoderOutputSizeMismatch;
    }
    if (!guardingOutputBoundary && produced > 0)
    {
        impl_->runningCrc = static_cast<uint32_t>(crc32(
            impl_->runningCrc,
            reinterpret_cast<const Bytef *>(output.data()),
            produced));
        impl_->uncompressedProduced += produced;
        outputProduced = produced;
    }

    if (inflateResult == Z_STREAM_END)
    {
        impl_->streamEnded = true;
        if (impl_->compressedConsumed != impl_->expectedCompressed)
        {
            return Error::DecoderInputSizeMismatch;
        }
        if (impl_->uncompressedProduced != impl_->expectedUncompressed)
        {
            return Error::DecoderOutputSizeMismatch;
        }
    }
    else if (inflateResult != Z_OK && inflateResult != Z_BUF_ERROR)
    {
        return Error::DecompressionData;
    }

    const bool outputWasExhausted = !guardingOutputBoundary &&
        impl_->stream.avail_out == 0 && impl_->uncompressedProduced < impl_->expectedUncompressed;
    impl_->UpdateProgress(outputWasExhausted);
    return Error::None;
}

Error MemberDecoder::Finish() noexcept
{
    if (!impl_ || !impl_->started)
    {
        return Error::DecoderNotStarted;
    }
    if (impl_->finished)
    {
        return Error::DecoderAlreadyFinished;
    }
    if (impl_->compressedConsumed != impl_->expectedCompressed)
    {
        return Error::DecoderInputSizeMismatch;
    }
    if (impl_->uncompressedProduced != impl_->expectedUncompressed)
    {
        return Error::DecoderOutputSizeMismatch;
    }

    if (impl_->method == 8 && !impl_->streamEnded)
    {
        std::array<uint8_t, 1> overflowGuard{};
        impl_->stream.next_in = Z_NULL;
        impl_->stream.avail_in = 0;
        impl_->stream.next_out = reinterpret_cast<Bytef *>(overflowGuard.data());
        impl_->stream.avail_out = 1;
        const int inflateResult = inflate(&impl_->stream, Z_FINISH);
        if (impl_->stream.avail_out != 1)
        {
            return Error::DecoderOutputSizeMismatch;
        }
        if (inflateResult != Z_STREAM_END)
        {
            return Error::DecompressionNotFinished;
        }
        impl_->streamEnded = true;
    }
    if (impl_->runningCrc != impl_->expectedCrc)
    {
        return Error::CrcMismatch;
    }

    EndInflate(impl_->stream, impl_->streamInitialized);
    impl_->finished = true;
    impl_->progress = DecoderProgress::Finished;
    return Error::None;
}

DecoderProgress MemberDecoder::Progress() const noexcept
{
    return impl_ ? impl_->progress : DecoderProgress::NotStarted;
}

uint32_t MemberDecoder::CompressedBytesConsumed() const noexcept
{
    return impl_ ? impl_->compressedConsumed : 0;
}

uint32_t MemberDecoder::UncompressedBytesProduced() const noexcept
{
    return impl_ ? impl_->uncompressedProduced : 0;
}

} // namespace kisak::iwd
