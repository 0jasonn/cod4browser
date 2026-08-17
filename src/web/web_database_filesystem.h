#pragma once

#include <cstdint>

using WebDatabaseFile = int;
constexpr WebDatabaseFile WEB_DATABASE_INVALID_FILE = -1;

WebDatabaseFile WebDatabaseFS_Open(const char *logicalPath);
std::int64_t WebDatabaseFS_Size(WebDatabaseFile file);
bool WebDatabaseFS_Seek(WebDatabaseFile file, std::uint32_t offset);
std::int32_t WebDatabaseFS_Read(
    WebDatabaseFile file, void *destination, std::uint32_t length);
void WebDatabaseFS_Close(WebDatabaseFile file);

