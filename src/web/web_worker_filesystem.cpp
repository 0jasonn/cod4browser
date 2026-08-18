#include <web/web_worker_filesystem.h>

#include <emscripten.h>

#include <cstddef>

namespace
{
static_assert(sizeof(WebWorkerDirectoryEntry) == 264);
static_assert(offsetof(WebWorkerDirectoryEntry, type) == 256);
static_assert(offsetof(WebWorkerDirectoryEntry, size) == 260);

EM_JS(int, WebWorkerFS_OpenJs, (const char *path), {
    const fs = globalThis.__KISAKCOD_SYNC_FS__;
    return fs && typeof fs.open === "function" ? fs.open(UTF8ToString(path)) : -1;
});

EM_JS(double, WebWorkerFS_SizeJs, (int file), {
    const fs = globalThis.__KISAKCOD_SYNC_FS__;
    return fs && typeof fs.size === "function" ? fs.size(file) : -1;
});

EM_JS(int, WebWorkerFS_SeekJs, (int file, std::uint32_t offset), {
    const fs = globalThis.__KISAKCOD_SYNC_FS__;
    return fs && typeof fs.seek === "function" && fs.seek(file, offset >>> 0) ? 1 : 0;
});

EM_JS(int, WebWorkerFS_ReadJs, (int file, void *destination, std::uint32_t length), {
    const fs = globalThis.__KISAKCOD_SYNC_FS__;
    return fs && typeof fs.read === "function"
        ? fs.read(file, destination >>> 0, length >>> 0)
        : -1;
});

EM_JS(void, WebWorkerFS_CloseJs, (int file), {
    const fs = globalThis.__KISAKCOD_SYNC_FS__;
    if (fs && typeof fs.close === "function") fs.close(file);
});

EM_JS(int, WebWorkerFS_StatJs,
    (const char *path, std::uint32_t *type, std::uint32_t *size), {
        const fs = globalThis.__KISAKCOD_SYNC_FS__;
        if (!fs || typeof fs.stat !== "function") return -1;
        const result = fs.stat(UTF8ToString(path));
        if (!result) return 0;
        const encodedType = result.type === "file" ? 1 :
            result.type === "directory" ? 2 : 0;
        if (!encodedType || !Number.isSafeInteger(result.size) ||
            result.size < 0 || result.size > 0xffff_ffff) return -1;
        HEAPU32[type >>> 2] = encodedType;
        HEAPU32[size >>> 2] = result.size >>> 0;
        return 1;
    });

EM_JS(int, WebWorkerFS_ListDirectoryJs,
    (const char *path, WebWorkerDirectoryEntry *output, std::uint32_t capacity), {
        const fs = globalThis.__KISAKCOD_SYNC_FS__;
        if (!fs || typeof fs.list !== "function") return -1;
        const entries = fs.list(UTF8ToString(path));
        if (!Array.isArray(entries) || entries.length > capacity) return -1;
        const stride = 264;
        for (let index = 0; index < entries.length; ++index) {
            const entry = entries[index];
            const type = entry?.type === "file" ? 1 :
                entry?.type === "directory" ? 2 : 0;
            if (!type || typeof entry.name !== "string" || entry.name.length === 0 ||
                lengthBytesUTF8(entry.name) > 255 ||
                !Number.isSafeInteger(entry.size) || entry.size < 0 ||
                entry.size > 0xffff_ffff) return -1;
            const address = (output >>> 0) + index * stride;
            stringToUTF8(entry.name, address, 256);
            HEAPU32[(address + 256) >>> 2] = type;
            HEAPU32[(address + 260) >>> 2] = entry.size >>> 0;
        }
        return entries.length;
    });
}

WebWorkerFile WebWorkerFS_Open(const char *logicalPath)
{
    return logicalPath ? WebWorkerFS_OpenJs(logicalPath) : WEB_WORKER_INVALID_FILE;
}

std::int64_t WebWorkerFS_Size(WebWorkerFile file)
{
    return static_cast<std::int64_t>(WebWorkerFS_SizeJs(file));
}

bool WebWorkerFS_Seek(WebWorkerFile file, std::uint32_t offset)
{
    return WebWorkerFS_SeekJs(file, offset) != 0;
}

std::int32_t WebWorkerFS_Read(
    WebWorkerFile file, void *destination, std::uint32_t length)
{
    if (!destination && length)
        return -1;
    if (!length)
        return 0;
    return WebWorkerFS_ReadJs(file, destination, length);
}

void WebWorkerFS_Close(WebWorkerFile file)
{
    if (file != WEB_WORKER_INVALID_FILE)
        WebWorkerFS_CloseJs(file);
}

bool WebWorkerFS_Stat(const char *logicalPath, WebWorkerFileStat &stat)
{
    stat = {};
    if (!logicalPath)
        return false;
    std::uint32_t type = 0;
    std::uint32_t size = 0;
    const int result = WebWorkerFS_StatJs(logicalPath, &type, &size);
    if (result <= 0)
        return false;
    stat.type = static_cast<WebWorkerFileType>(type);
    stat.size = size;
    return stat.type == WebWorkerFileType::File ||
        stat.type == WebWorkerFileType::Directory;
}

bool WebWorkerFS_ListDirectory(
    const char *logicalPath, std::vector<WebWorkerDirectoryEntry> &entries)
{
    entries.clear();
    if (!logicalPath)
        return false;
    try
    {
        entries.resize(WEB_WORKER_MAX_DIRECTORY_ENTRIES);
    }
    catch (...)
    {
        return false;
    }
    const int count = WebWorkerFS_ListDirectoryJs(
        logicalPath, entries.data(), static_cast<std::uint32_t>(entries.size()));
    if (count < 0)
    {
        entries.clear();
        return false;
    }
    entries.resize(static_cast<std::size_t>(count));
    return true;
}
