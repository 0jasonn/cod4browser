#pragma once

#include <cstdint>

// Narrow synchronous file boundary used by the canonical database runtime.
// Browser storage and Worker details remain in the platform implementation.
using DBPlatformFile = std::int32_t;
constexpr DBPlatformFile DB_PLATFORM_INVALID_FILE = -1;

void DB_PlatformBuildZonePath(
    const char *zoneName, std::uint32_t size, char *filename);
DBPlatformFile DB_PlatformOpenFile(const char *logicalPath);
std::int64_t DB_PlatformFileSize(DBPlatformFile file);
std::int32_t DB_PlatformReadFile(
    DBPlatformFile file, void *destination, std::uint32_t length);
void DB_PlatformCloseFile(DBPlatformFile file);

void *DB_PlatformFileToOpaque(DBPlatformFile file);
DBPlatformFile DB_PlatformFileFromOpaque(void *file);
