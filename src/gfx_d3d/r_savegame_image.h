#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

struct SaveHeader;
inline constexpr int SAVEGAME_IMAGE_SIZE = 512;
inline constexpr std::size_t SAVEGAME_JPEG_MAX_BYTES = 2u * 1024u * 1024u;
void R_SaveGameThumbnail(const SaveHeader &header);

// Check dimensions before handing untrusted JPEG data to a platform decoder.
// The native save convention is an 8-bit 512x512 image; entropy decoding and
// JPEG validity remain the codec's responsibility.
inline bool R_IsSaveGameJpeg(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < 4 || bytes.size() > SAVEGAME_JPEG_MAX_BYTES ||
        bytes[0] != 0xff || bytes[1] != 0xd8) return false;
    bool frame = false;
    for (std::size_t at = 2; at < bytes.size();)
    {
        if (bytes[at++] != 0xff) return false;
        while (at < bytes.size() && bytes[at] == 0xff) ++at;
        if (at == bytes.size()) return false;
        const unsigned marker = bytes[at++];
        if (marker == 0 || marker == 0xd8 || marker == 0xd9 ||
            (marker >= 0xd0 && marker <= 0xd7) || bytes.size() - at < 2) return false;
        const unsigned length = (bytes[at] << 8) | bytes[at + 1];
        if (length < 2 || length > bytes.size() - at) return false;
        if (marker == 0xda) return frame;
        if (marker >= 0xc0 && marker <= 0xcf &&
            marker != 0xc4 && marker != 0xc8 && marker != 0xcc)
        {
            if (frame || marker > 0xc2 || length < 8 || bytes[at + 2] != 8 ||
                ((bytes[at + 3] << 8) | bytes[at + 4]) != SAVEGAME_IMAGE_SIZE ||
                ((bytes[at + 5] << 8) | bytes[at + 6]) != SAVEGAME_IMAGE_SIZE ||
                (bytes[at + 7] != 1 && bytes[at + 7] != 3) ||
                length != 8u + 3u * bytes[at + 7]) return false;
            frame = true;
        }
        at += length;
    }
    return false;
}
