#pragma once

#include <cstdint>

enum class WebAssetProbeResult : int32_t
{
    Success = 0,
    InvalidArgument = 1,
    UnsupportedSize = 2,
    LocalizationSize = 10,
    LocalizationContent = 11,
    LocalizationLanguage = 12,
    IwdHeader = 20,
    IwdEocd = 21,
    IwdMultiDisk = 22,
    IwdZip64 = 23,
    IwdEmpty = 24,
    IwdCentralRange = 25,
    IwdCentralWindow = 26,
    IwdCentralHeader = 27,
    IwdEntryMismatch = 28,
    IwdEncrypted = 29,
    IwdCompression = 30,
    FastfileHeader = 40,
    FastfileAuthenticated = 41,
    FastfileVersion = 42,
    FastfileCompression = 43,
};

// These probes operate on caller-owned, bounded byte windows. Browser file
// selection and persistent storage remain asynchronous JavaScript platform
// services; large retail archives are never copied wholesale into Wasm.
extern "C" int32_t KisakWeb_ProbeLocalization(
    const uint8_t *data,
    uint32_t length,
    uint32_t fileSize);

extern "C" int32_t KisakWeb_ProbeIwd(
    const uint8_t *head,
    uint32_t headLength,
    const uint8_t *tail,
    uint32_t tailLength,
    uint32_t tailOffset,
    const uint8_t *central,
    uint32_t centralLength,
    uint32_t centralOffset,
    uint32_t fileSize);

// Validates only the bounded 12-byte IWff header and the following two-byte
// zlib header. It deliberately does not inflate or traverse a retail zone.
extern "C" int32_t KisakWeb_ProbeFastfileHeader(
    const uint8_t *head,
    uint32_t headLength,
    uint32_t fileSize);
