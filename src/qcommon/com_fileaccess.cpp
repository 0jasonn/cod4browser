#include <universal/q_shared.h>
#include "com_fileaccess.h"

#include "qcommon.h"

#if defined(KISAK_WEB)

#include <web/web_worker_filesystem.h>

#include <limits>
#include <new>

namespace
{
struct WebFileHandle
{
    WebWorkerFile file = WEB_WORKER_INVALID_FILE;
    std::uint32_t size = 0;
    std::uint32_t position = 0;
};

WebFileHandle *AsWebFile(FILE *file)
{
    return reinterpret_cast<WebFileHandle *>(file);
}

FILE *OpenWebFile(const char *filename)
{
    if (!filename || !*filename)
        return nullptr;
    const WebWorkerFile descriptor = WebWorkerFS_Open(filename);
    if (descriptor == WEB_WORKER_INVALID_FILE)
        return nullptr;
    const std::int64_t size = WebWorkerFS_Size(descriptor);
    // The canonical filesystem reports file lengths through signed int APIs.
    // Reject files that cannot be represented instead of wrapping them into a
    // negative length after a successful open.
    if (size < 0 || size > std::numeric_limits<int>::max())
    {
        WebWorkerFS_Close(descriptor);
        return nullptr;
    }
    WebFileHandle *handle = new (std::nothrow) WebFileHandle{
        descriptor, static_cast<std::uint32_t>(size), 0u};
    if (!handle)
    {
        WebWorkerFS_Close(descriptor);
        return nullptr;
    }
    return reinterpret_cast<FILE *>(handle);
}
}

int __cdecl FS_FileGetFileSize(FILE *file)
{
    return file ? static_cast<int>(AsWebFile(file)->size) : -1;
}

uint32_t __cdecl FS_FileRead(void *ptr, uint32_t len, FILE *stream)
{
    if (!stream || (!ptr && len))
        return static_cast<std::uint32_t>(-1);
    WebFileHandle *file = AsWebFile(stream);
    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_READ);
    const std::int32_t read = WebWorkerFS_Read(file->file, ptr, len);
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_READ);
    if (read < 0)
        return static_cast<std::uint32_t>(-1);
    file->position += static_cast<std::uint32_t>(read);
    return static_cast<std::uint32_t>(read);
}

uint32_t __cdecl FS_FileWrite(const void *, uint32_t, FILE *)
{
    // Imported installation data is read-only. Browser save/config storage
    // will enter through a separate writable home-path primitive.
    return 0;
}

FILE *__cdecl FS_FileOpenReadBinary(const char *filename)
{
    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_OPEN);
    FILE *file = OpenWebFile(filename);
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_OPEN);
    return file;
}

FILE *__cdecl FS_FileOpenReadText(const char *filename)
{
    return FS_FileOpenReadBinary(filename);
}

FILE *__cdecl FS_FileOpenWriteBinary(const char *) { return nullptr; }
FILE *__cdecl FS_FileOpenAppendText(const char *) { return nullptr; }
FILE *__cdecl FS_FileOpenWriteText(const char *) { return nullptr; }
FILE *FS_FileOpenWriteReadBinary(const char *) { return nullptr; }

void __cdecl FS_FileClose(FILE *stream)
{
    if (!stream)
        return;
    WebFileHandle *file = AsWebFile(stream);
    WebWorkerFS_Close(file->file);
    delete file;
}

int __cdecl FS_FileSeek(FILE *file, int offset, int whence)
{
    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_SEEK);
    const int result = FileWrapper_Seek(file, offset, whence);
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_SEEK);
    return result;
}

int __cdecl FileWrapper_Seek(FILE *stream, int offset, int origin)
{
    if (!stream)
        return -1;
    WebFileHandle *file = AsWebFile(stream);
    std::int64_t target = 0;
    switch (origin)
    {
    case 0: target = static_cast<std::int64_t>(file->position) + offset; break;
    case 1: target = static_cast<std::int64_t>(file->size) + offset; break;
    case 2: target = offset; break;
    default: return -1;
    }
    if (target < 0 || target > file->size ||
        !WebWorkerFS_Seek(file->file, static_cast<std::uint32_t>(target)))
        return -1;
    file->position = static_cast<std::uint32_t>(target);
    return 0;
}

int __cdecl FileWrapper_GetFileSize(FILE *file)
{
    return FS_FileGetFileSize(file);
}

uint32_t FS_FileTell(FILE *file)
{
    return file ? AsWebFile(file)->position : 0u;
}

#else

int __cdecl FS_FileGetFileSize(FILE *file)
{
    return FileWrapper_GetFileSize(file);
}

uint32_t __cdecl FS_FileRead(void *ptr, uint32_t len, FILE *stream)
{
    uint32_t read_size; // [esp+0h] [ebp-4h]

    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_READ);
    read_size = fread(ptr, 1u, len, stream);
#ifdef _DEBUG
    if (ferror(stream))
    {
        iassert(0);
    }
    //if (feof(stream))
    //{
    //    iassert(0);
    //}
#endif
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_READ);
    return read_size;
}

uint32_t __cdecl FS_FileWrite(const void *ptr, uint32_t len, FILE *stream)
{
    return fwrite(ptr, 1u, len, stream);
}

FILE *__cdecl FS_FileOpenReadBinary(const char *filename)
{
    FILE *file; // [esp+0h] [ebp-4h]

    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_OPEN);
    file = fopen(filename, "rb");
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_OPEN);
    return file;
}

FILE *__cdecl FS_FileOpenReadText(const char *filename)
{
    FILE *file; // [esp+0h] [ebp-4h]

    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_OPEN);
    file = fopen(filename, "rt");
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_OPEN);
    return file;
}

FILE *__cdecl FS_FileOpenWriteBinary(const char *filename)
{
    FILE *file; // [esp+0h] [ebp-4h]

    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_OPEN);
    file = fopen(filename, "wb");
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_OPEN);
    return file;
}

FILE *__cdecl FS_FileOpenAppendText(const char *filename)
{
    FILE *file; // [esp+0h] [ebp-4h]

    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_OPEN);
    file = fopen(filename, "at");
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_OPEN);
    return file;
}

FILE *__cdecl FS_FileOpenWriteText(const char *filename)
{
    FILE *file; // [esp+0h] [ebp-4h]

    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_OPEN);
    file = fopen(filename, "w+t");
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_OPEN);
    return file;
}

FILE *FS_FileOpenWriteReadBinary(const char *filename)
{
    FILE *file;

    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_OPEN);
    file = fopen(filename, "w+b"); // KISAKTODO: unsure if flag is accurate, it uses CreateFileA()
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_OPEN);
    return file;
}

int __cdecl FS_FileSeek(FILE *file, int offset, int whence)
{
    int seek; // [esp+4h] [ebp-4h]

    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_SEEK);
    seek = FileWrapper_Seek(file, offset, whence);
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_SEEK);
    return seek;
}

int __cdecl FileWrapper_Seek(FILE *h, int offset, int origin)
{
    const char *v4; // eax

    switch (origin)
    {
    case 0:
        return fseek(h, offset, 1);
    case 1:
        return fseek(h, offset, 2);
    case 2:
        return fseek(h, offset, 0);
    }
    if (!alwaysfails)
    {
        v4 = va("Bad origin %i in FS_Seek", origin);
        MyAssertHandler("c:\\trees\\cod3\\src\\qcommon\\../universal/com_files_wrapper_stdio.h", 96, 0, v4);
    }
    return 0;
}

int __cdecl FileWrapper_GetFileSize(FILE *h)
{
    int startPos; // [esp+0h] [ebp-8h]
    int fileSize; // [esp+4h] [ebp-4h]

    startPos = ftell(h);
    fseek(h, 0, 2);
    fileSize = ftell(h);
    fseek(h, startPos, 0);
    return fileSize;
}

#ifdef KISAK_SP
#if defined(KISAK_WEB)
uint32_t FS_FileTell(FILE *file)
{
    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_SEEK);
    const long position = ftell(file);
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_SEEK);
    return position < 0 ? 0u : static_cast<uint32_t>(position);
}

#else
#include <Windows.h>
#include <fileapi.h>
uint32_t FS_FileTell(FILE *file)
{
    _LARGE_INTEGER v2; // [sp+50h] [-20h] BYREF

    ProfLoad_BeginTrackedValue(MAP_PROFILE_FILE_SEEK);
    v2.QuadPart = 0;
    LARGE_INTEGER move;
    move.QuadPart = 0;
    SetFilePointerEx(0, move, &v2, 1u);
    ProfLoad_EndTrackedValue(MAP_PROFILE_FILE_SEEK);
    return v2.LowPart;
}
#endif

#endif // KISAK_SP

void __cdecl FS_FileClose(FILE *stream)
{
    fclose(stream);
}

#endif // KISAK_WEB
