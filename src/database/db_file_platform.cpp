#include <database/db_file_platform.h>

#if defined(KISAK_WEB) || defined(KISAK_DB_SYNC_FILE_TEST)
#include <universal/q_shared.h>
#include <stringed/stringed_hooks.h>
#include <web/web_worker_filesystem.h>

#include <cstdio>

void DB_PlatformBuildZonePath(
    const char *zoneName, std::uint32_t size, char *filename)
{
    if (!filename || !size) return;
    std::snprintf(filename, size, "zone/%s/%s.ff",
        SEH_GetLanguageName(SEH_GetCurrentLanguage()), zoneName ? zoneName : "");
    filename[size - 1] = '\0';
}

DBPlatformFile DB_PlatformOpenFile(const char *logicalPath)
{
    return WebWorkerFS_Open(logicalPath);
}

std::int64_t DB_PlatformFileSize(DBPlatformFile file)
{
    return WebWorkerFS_Size(file);
}

std::int32_t DB_PlatformReadFile(
    DBPlatformFile file, void *destination, std::uint32_t length)
{
    return WebWorkerFS_Read(file, destination, length);
}

void DB_PlatformCloseFile(DBPlatformFile file)
{
    WebWorkerFS_Close(file);
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
