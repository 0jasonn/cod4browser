#include <database/db_file_platform.h>

#if defined(KISAK_WEB) || defined(KISAK_DB_SYNC_FILE_TEST)
#include <web/web_database_filesystem.h>

#include <cstdio>

void DB_PlatformBuildZonePath(
    const char *zoneName, std::uint32_t size, char *filename)
{
    if (!filename || !size) return;
    std::snprintf(filename, size, "zone/english/%s.ff", zoneName ? zoneName : "");
    filename[size - 1] = '\0';
}

DBPlatformFile DB_PlatformOpenFile(const char *logicalPath)
{
    return WebDatabaseFS_Open(logicalPath);
}

std::int64_t DB_PlatformFileSize(DBPlatformFile file)
{
    return WebDatabaseFS_Size(file);
}

std::int32_t DB_PlatformReadFile(
    DBPlatformFile file, void *destination, std::uint32_t length)
{
    return WebDatabaseFS_Read(file, destination, length);
}

void DB_PlatformCloseFile(DBPlatformFile file)
{
    WebDatabaseFS_Close(file);
}

void *DB_PlatformFileToOpaque(DBPlatformFile file)
{
    return reinterpret_cast<void *>(static_cast<std::uintptr_t>(file) + 1u);
}

DBPlatformFile DB_PlatformFileFromOpaque(void *file)
{
    return static_cast<DBPlatformFile>(reinterpret_cast<std::uintptr_t>(file) - 1u);
}
#else
#error "A native DB file platform implementation is required for this target"
#endif
