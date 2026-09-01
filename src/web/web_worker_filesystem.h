#pragma once

#include <cstdint>
#include <vector>

using WebWorkerFile = int;

inline constexpr WebWorkerFile WEB_WORKER_INVALID_FILE = -1;
inline constexpr std::uint32_t WEB_WORKER_MAX_DIRECTORY_ENTRIES = 8191;
inline constexpr std::uint32_t WEB_WORKER_MAX_ENTRY_NAME_BYTES = 255;

enum class WebWorkerFileType : std::uint32_t
{
    Missing = 0,
    File = 1,
    Directory = 2,
};

struct WebWorkerFileStat
{
    WebWorkerFileType type = WebWorkerFileType::Missing;
    std::uint32_t size = 0;
};

struct WebWorkerDirectoryEntry
{
    char name[WEB_WORKER_MAX_ENTRY_NAME_BYTES + 1]{};
    WebWorkerFileType type = WebWorkerFileType::Missing;
    std::uint32_t size = 0;
};

WebWorkerFile WebWorkerFS_Open(const char *logicalPath);
WebWorkerFile WebWorkerFS_OpenWrite(const char *logicalPath, bool append);
std::int64_t WebWorkerFS_Size(WebWorkerFile file);
bool WebWorkerFS_Seek(WebWorkerFile file, std::uint32_t offset);
std::int32_t WebWorkerFS_Read(
    WebWorkerFile file, void *destination, std::uint32_t length);
std::int32_t WebWorkerFS_Write(
    WebWorkerFile file, const void *source, std::uint32_t length);
void WebWorkerFS_Close(WebWorkerFile file);

bool WebWorkerFS_Mkdir(const char *logicalPath);
bool WebWorkerFS_Remove(const char *logicalPath);
bool WebWorkerFS_RemoveTree(const char *logicalPath);
bool WebWorkerFS_Rename(const char *from, const char *to);

bool WebWorkerFS_Stat(const char *logicalPath, WebWorkerFileStat &stat);
bool WebWorkerFS_ListDirectory(
    const char *logicalPath, std::vector<WebWorkerDirectoryEntry> &entries);
