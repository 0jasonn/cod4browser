#include <web/web_database_filesystem.h>

#include <emscripten.h>

EM_JS(int, WebDatabaseFS_OpenJs, (const char *path), {
    const fs = globalThis.__KISAKCOD_SYNC_FS__;
    return fs && typeof fs.open === "function" ? fs.open(UTF8ToString(path)) : -1;
});

EM_JS(double, WebDatabaseFS_SizeJs, (int file), {
    const fs = globalThis.__KISAKCOD_SYNC_FS__;
    return fs && typeof fs.size === "function" ? fs.size(file) : -1;
});

EM_JS(int, WebDatabaseFS_SeekJs, (int file, std::uint32_t offset), {
    const fs = globalThis.__KISAKCOD_SYNC_FS__;
    return fs && typeof fs.seek === "function" && fs.seek(file, offset >>> 0) ? 1 : 0;
});

EM_JS(int, WebDatabaseFS_ReadJs, (int file, void *destination, std::uint32_t length), {
    const fs = globalThis.__KISAKCOD_SYNC_FS__;
    return fs && typeof fs.read === "function"
        ? fs.read(file, destination >>> 0, length >>> 0)
        : -1;
});

EM_JS(void, WebDatabaseFS_CloseJs, (int file), {
    const fs = globalThis.__KISAKCOD_SYNC_FS__;
    if (fs && typeof fs.close === "function") fs.close(file);
});

WebDatabaseFile WebDatabaseFS_Open(const char *logicalPath)
{
    return logicalPath ? WebDatabaseFS_OpenJs(logicalPath) : WEB_DATABASE_INVALID_FILE;
}

std::int64_t WebDatabaseFS_Size(WebDatabaseFile file)
{
    return static_cast<std::int64_t>(WebDatabaseFS_SizeJs(file));
}

bool WebDatabaseFS_Seek(WebDatabaseFile file, std::uint32_t offset)
{
    return WebDatabaseFS_SeekJs(file, offset) != 0;
}

std::int32_t WebDatabaseFS_Read(
    WebDatabaseFile file, void *destination, std::uint32_t length)
{
    return destination && length ? WebDatabaseFS_ReadJs(file, destination, length) : -1;
}

void WebDatabaseFS_Close(WebDatabaseFile file)
{
    if (file != WEB_DATABASE_INVALID_FILE) WebDatabaseFS_CloseJs(file);
}

