#include <web/web_asset_probe.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace
{
constexpr uint32_t LOCALIZATION_BUFFER_SIZE = 0x1000;
constexpr uint32_t PROBE_WINDOW_SIZE = 4096;
constexpr uint32_t ZIP_EOCD_SIZE = 22;
constexpr uint32_t ZIP_MAX_COMMENT_SIZE = 0xffff;
constexpr uint32_t ZIP_TAIL_WINDOW_SIZE = ZIP_EOCD_SIZE + ZIP_MAX_COMMENT_SIZE;
constexpr uint32_t ZIP_LOCAL_HEADER_SIZE = 30;
constexpr uint32_t ZIP_CENTRAL_HEADER_SIZE = 46;
constexpr uint32_t FASTFILE_PROBE_SIZE = 14;
constexpr uint32_t FASTFILE_VERSION = 5;

constexpr std::array<uint8_t, 8> FASTFILE_UNSIGNED_MAGIC = {
    'I', 'W', 'f', 'f', 'u', '1', '0', '0',
};
constexpr std::array<uint8_t, 8> FASTFILE_AUTHENTICATED_MAGIC = {
    'I', 'W', 'f', 'f', '0', '1', '0', '0',
};

constexpr uint32_t ZIP_LOCAL_SIGNATURE = 0x04034b50;
constexpr uint32_t ZIP_CENTRAL_SIGNATURE = 0x02014b50;
constexpr uint32_t ZIP_EOCD_SIGNATURE = 0x06054b50;

constexpr std::array<std::string_view, 15> SUPPORTED_LANGUAGES = {
    "english",
    "french",
    "german",
    "italian",
    "spanish",
    "british",
    "russian",
    "polish",
    "korean",
    "taiwanese",
    "japanese",
    "chinese",
    "thai",
    "leet",
    "czech",
};

int32_t Result(WebAssetProbeResult result)
{
    return static_cast<int32_t>(result);
}

bool ReadU16(std::span<const uint8_t> bytes, std::size_t offset, uint16_t &value)
{
    if (offset > bytes.size() || bytes.size() - offset < 2)
    {
        return false;
    }
    value = static_cast<uint16_t>(bytes[offset]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
    return true;
}

bool ReadU32(std::span<const uint8_t> bytes, std::size_t offset, uint32_t &value)
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

bool IsValidUtf8(std::span<const uint8_t> bytes)
{
    std::size_t index = 0;
    while (index < bytes.size())
    {
        const uint8_t lead = bytes[index];
        if (lead < 0x80)
        {
            if ((lead < 0x20 && lead != '\n' && lead != '\r' && lead != '\t') || lead == 0x7f)
            {
                return false;
            }
            ++index;
            continue;
        }

        std::size_t continuationCount = 0;
        uint32_t codePoint = 0;
        uint32_t minimumCodePoint = 0;
        if ((lead & 0xe0) == 0xc0)
        {
            continuationCount = 1;
            codePoint = lead & 0x1f;
            minimumCodePoint = 0x80;
        }
        else if ((lead & 0xf0) == 0xe0)
        {
            continuationCount = 2;
            codePoint = lead & 0x0f;
            minimumCodePoint = 0x800;
        }
        else if ((lead & 0xf8) == 0xf0)
        {
            continuationCount = 3;
            codePoint = lead & 0x07;
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
            if ((next & 0xc0) != 0x80)
            {
                return false;
            }
            codePoint = (codePoint << 6) | (next & 0x3f);
        }

        if (codePoint < minimumCodePoint || codePoint > 0x10ffff ||
            (codePoint >= 0xd800 && codePoint <= 0xdfff))
        {
            return false;
        }
        index += continuationCount + 1;
    }
    return true;
}

bool IsWhitespace(uint8_t byte)
{
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

bool IsSupportedCompression(uint16_t method)
{
    return method == 0 || method == 8;
}
} // namespace

extern "C" int32_t KisakWeb_ProbeLocalization(
    const uint8_t *data,
    uint32_t length,
    uint32_t fileSize)
{
    if (!data || length != fileSize)
    {
        return Result(WebAssetProbeResult::InvalidArgument);
    }
    if (fileSize == 0 || fileSize >= LOCALIZATION_BUFFER_SIZE)
    {
        return Result(WebAssetProbeResult::LocalizationSize);
    }

    const std::span<const uint8_t> bytes(data, length);
    if (!IsValidUtf8(bytes) || std::find(bytes.begin(), bytes.end(), uint8_t{0}) != bytes.end())
    {
        return Result(WebAssetProbeResult::LocalizationContent);
    }

    const auto lineEnd = std::find(bytes.begin(), bytes.end(), uint8_t{'\n'});
    if (lineEnd == bytes.end())
    {
        return Result(WebAssetProbeResult::LocalizationContent);
    }
    std::size_t languageLength = static_cast<std::size_t>(lineEnd - bytes.begin());
    if (languageLength > 0 && bytes[languageLength - 1] == '\r')
    {
        --languageLength;
    }
    const std::string_view language(
        reinterpret_cast<const char *>(bytes.data()),
        languageLength);
    if (std::find(SUPPORTED_LANGUAGES.begin(), SUPPORTED_LANGUAGES.end(), language) ==
        SUPPORTED_LANGUAGES.end())
    {
        return Result(WebAssetProbeResult::LocalizationLanguage);
    }

    const std::size_t payloadOffset = static_cast<std::size_t>(lineEnd - bytes.begin()) + 1;
    const bool hasPayload = std::any_of(
        bytes.begin() + static_cast<std::ptrdiff_t>(payloadOffset),
        bytes.end(),
        [](uint8_t byte) { return !IsWhitespace(byte); });
    return Result(
        hasPayload ? WebAssetProbeResult::Success : WebAssetProbeResult::LocalizationContent);
}

extern "C" int32_t KisakWeb_ProbeIwd(
    const uint8_t *head,
    uint32_t headLength,
    const uint8_t *tail,
    uint32_t tailLength,
    uint32_t tailOffset,
    const uint8_t *central,
    uint32_t centralLength,
    uint32_t centralOffset,
    uint32_t fileSize)
{
    if (!head || !tail || !central || fileSize < ZIP_EOCD_SIZE)
    {
        return Result(WebAssetProbeResult::InvalidArgument);
    }
    const uint32_t expectedHeadLength = std::min(fileSize, PROBE_WINDOW_SIZE);
    const uint32_t expectedTailLength = std::min(fileSize, ZIP_TAIL_WINDOW_SIZE);
    if (headLength != expectedHeadLength || tailLength != expectedTailLength ||
        tailOffset != fileSize - expectedTailLength)
    {
        return Result(WebAssetProbeResult::InvalidArgument);
    }

    const std::span<const uint8_t> headBytes(head, headLength);
    const std::span<const uint8_t> tailBytes(tail, tailLength);
    const std::span<const uint8_t> centralBytes(central, centralLength);

    uint32_t signature = 0;
    if (!ReadU32(headBytes, 0, signature) || signature != ZIP_LOCAL_SIGNATURE ||
        headBytes.size() < ZIP_LOCAL_HEADER_SIZE)
    {
        return Result(WebAssetProbeResult::IwdHeader);
    }

    std::size_t eocdPosition = tailBytes.size();
    if (tailBytes.size() >= ZIP_EOCD_SIZE)
    {
        for (std::size_t position = tailBytes.size() - ZIP_EOCD_SIZE;; --position)
        {
            uint32_t candidateSignature = 0;
            uint16_t commentLength = 0;
            if (ReadU32(tailBytes, position, candidateSignature) &&
                candidateSignature == ZIP_EOCD_SIGNATURE &&
                ReadU16(tailBytes, position + 20, commentLength) &&
                commentLength == tailBytes.size() - position - ZIP_EOCD_SIZE)
            {
                eocdPosition = position;
                break;
            }
            if (position == 0)
            {
                break;
            }
        }
    }
    if (eocdPosition == tailBytes.size())
    {
        return Result(WebAssetProbeResult::IwdEocd);
    }

    uint16_t diskNumber = 0;
    uint16_t centralDisk = 0;
    uint16_t entriesOnDisk = 0;
    uint16_t entriesTotal = 0;
    uint32_t centralSize = 0;
    uint32_t recordedCentralOffset = 0;
    if (!ReadU16(tailBytes, eocdPosition + 4, diskNumber) ||
        !ReadU16(tailBytes, eocdPosition + 6, centralDisk) ||
        !ReadU16(tailBytes, eocdPosition + 8, entriesOnDisk) ||
        !ReadU16(tailBytes, eocdPosition + 10, entriesTotal) ||
        !ReadU32(tailBytes, eocdPosition + 12, centralSize) ||
        !ReadU32(tailBytes, eocdPosition + 16, recordedCentralOffset))
    {
        return Result(WebAssetProbeResult::IwdEocd);
    }
    if (diskNumber != 0 || centralDisk != 0 || entriesOnDisk != entriesTotal)
    {
        return Result(WebAssetProbeResult::IwdMultiDisk);
    }
    if (entriesTotal == 0xffff || centralSize == 0xffffffff ||
        recordedCentralOffset == 0xffffffff)
    {
        return Result(WebAssetProbeResult::IwdZip64);
    }
    if (entriesTotal == 0)
    {
        return Result(WebAssetProbeResult::IwdEmpty);
    }

    const uint32_t absoluteEocd = tailOffset + static_cast<uint32_t>(eocdPosition);
    if (recordedCentralOffset > absoluteEocd ||
        centralSize != absoluteEocd - recordedCentralOffset ||
        centralSize < ZIP_CENTRAL_HEADER_SIZE ||
        entriesTotal > centralSize / ZIP_CENTRAL_HEADER_SIZE)
    {
        return Result(WebAssetProbeResult::IwdCentralRange);
    }

    const uint32_t expectedCentralLength = std::min(centralSize, PROBE_WINDOW_SIZE);
    if (centralOffset != recordedCentralOffset || centralLength != expectedCentralLength ||
        centralBytes.size() < ZIP_CENTRAL_HEADER_SIZE)
    {
        return Result(WebAssetProbeResult::IwdCentralWindow);
    }
    if (!ReadU32(centralBytes, 0, signature) || signature != ZIP_CENTRAL_SIGNATURE)
    {
        return Result(WebAssetProbeResult::IwdCentralHeader);
    }

    uint16_t localFlags = 0;
    uint16_t localMethod = 0;
    uint16_t localNameLength = 0;
    uint16_t localExtraLength = 0;
    uint16_t centralFlags = 0;
    uint16_t centralMethod = 0;
    uint16_t centralNameLength = 0;
    uint16_t centralExtraLength = 0;
    uint16_t centralCommentLength = 0;
    uint16_t entryDisk = 0;
    uint32_t localOffset = 0;
    if (!ReadU16(headBytes, 6, localFlags) || !ReadU16(headBytes, 8, localMethod) ||
        !ReadU16(headBytes, 26, localNameLength) || !ReadU16(headBytes, 28, localExtraLength) ||
        !ReadU16(centralBytes, 8, centralFlags) || !ReadU16(centralBytes, 10, centralMethod) ||
        !ReadU16(centralBytes, 28, centralNameLength) ||
        !ReadU16(centralBytes, 30, centralExtraLength) ||
        !ReadU16(centralBytes, 32, centralCommentLength) ||
        !ReadU16(centralBytes, 34, entryDisk) || !ReadU32(centralBytes, 42, localOffset))
    {
        return Result(WebAssetProbeResult::IwdEntryMismatch);
    }
    if (localNameLength == 0 || localNameLength != centralNameLength ||
        localFlags != centralFlags || localMethod != centralMethod || entryDisk != 0 ||
        localOffset != 0)
    {
        return Result(WebAssetProbeResult::IwdEntryMismatch);
    }

    const std::size_t localRecordLength = ZIP_LOCAL_HEADER_SIZE +
        static_cast<std::size_t>(localNameLength) + localExtraLength;
    const std::size_t centralRecordLength = ZIP_CENTRAL_HEADER_SIZE +
        static_cast<std::size_t>(centralNameLength) + centralExtraLength + centralCommentLength;
    if (localRecordLength > headBytes.size() || centralRecordLength > centralBytes.size())
    {
        return Result(WebAssetProbeResult::IwdEntryMismatch);
    }
    if (!std::equal(
            headBytes.begin() + ZIP_LOCAL_HEADER_SIZE,
            headBytes.begin() + ZIP_LOCAL_HEADER_SIZE + localNameLength,
            centralBytes.begin() + ZIP_CENTRAL_HEADER_SIZE))
    {
        return Result(WebAssetProbeResult::IwdEntryMismatch);
    }
    if ((localFlags & 0x0041) != 0)
    {
        return Result(WebAssetProbeResult::IwdEncrypted);
    }
    if (!IsSupportedCompression(localMethod))
    {
        return Result(WebAssetProbeResult::IwdCompression);
    }

    return Result(WebAssetProbeResult::Success);
}

extern "C" int32_t KisakWeb_ProbeFastfileHeader(
    const uint8_t *head,
    uint32_t headLength,
    uint32_t fileSize)
{
    if (!head || headLength != std::min(fileSize, FASTFILE_PROBE_SIZE))
    {
        return Result(WebAssetProbeResult::InvalidArgument);
    }
    if (fileSize < FASTFILE_PROBE_SIZE)
    {
        return Result(WebAssetProbeResult::FastfileHeader);
    }

    const std::span<const uint8_t> bytes(head, headLength);
    if (std::equal(
            FASTFILE_AUTHENTICATED_MAGIC.begin(),
            FASTFILE_AUTHENTICATED_MAGIC.end(),
            bytes.begin()))
    {
        return Result(WebAssetProbeResult::FastfileAuthenticated);
    }
    if (!std::equal(
            FASTFILE_UNSIGNED_MAGIC.begin(),
            FASTFILE_UNSIGNED_MAGIC.end(),
            bytes.begin()))
    {
        return Result(WebAssetProbeResult::FastfileHeader);
    }

    uint32_t version = 0;
    if (!ReadU32(bytes, 8, version) || version != FASTFILE_VERSION)
    {
        return Result(WebAssetProbeResult::FastfileVersion);
    }
    const uint8_t compressionMethod = bytes[12];
    const uint8_t compressionFlags = bytes[13];
    const uint16_t compressionHeader = static_cast<uint16_t>(
        static_cast<uint16_t>(compressionMethod) << 8u) |
        static_cast<uint16_t>(compressionFlags);
    if ((compressionMethod & 0x0fu) != 8u ||
        (compressionMethod >> 4u) > 7u ||
        (compressionFlags & 0x20u) != 0u ||
        compressionHeader % 31u != 0u)
    {
        return Result(WebAssetProbeResult::FastfileCompression);
    }
    return Result(WebAssetProbeResult::Success);
}
