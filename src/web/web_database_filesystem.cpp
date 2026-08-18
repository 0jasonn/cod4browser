#include <web/web_database_filesystem.h>
#include <web/web_worker_filesystem.h>

WebDatabaseFile WebDatabaseFS_Open(const char *logicalPath)
{
    return WebWorkerFS_Open(logicalPath);
}

std::int64_t WebDatabaseFS_Size(WebDatabaseFile file)
{
    return WebWorkerFS_Size(file);
}

bool WebDatabaseFS_Seek(WebDatabaseFile file, std::uint32_t offset)
{
    return WebWorkerFS_Seek(file, offset);
}

std::int32_t WebDatabaseFS_Read(
    WebDatabaseFile file, void *destination, std::uint32_t length)
{
    return WebWorkerFS_Read(file, destination, length);
}

void WebDatabaseFS_Close(WebDatabaseFile file)
{
    WebWorkerFS_Close(file);
}
