#include <universal/q_shared.h>
#include <universal/com_files.h>
#include <client/client.h>
#include <game/savedevice.h>
#include <gfx_d3d/r_savegame_image.h>
#include <qcommon/com_fileaccess.h>
#include <qcommon/qcommon.h>
#include <qcommon/system_files.h>
#include <qcommon/threads.h>
#include <qcommon/unzip.h>
#include <script/scr_readwrite.h>
#include <web/web_worker_filesystem.h>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

// Link the real com_files.cpp and savedevice_pc.cpp. Only platform I/O and
// unrelated engine services are replaced; files live in Emscripten's MEMFS.
static bool admitRename = false;
static bool refuseWrite = false;
static int thumbnails = 0;
static dvar_t home{}, debug{};
static searchpath_s searchPath{};
const dvar_t *useFastFile = &debug;

static std::string Path(const char *path)
{
    std::string result(path);
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

void MyAssertHandler(const char *, int, int, const char *, ...) { std::abort(); }
void Com_Error(errorParm_t, const char *, ...) { std::abort(); }
void Com_Printf(int, const char *, ...) {}
void Com_PrintWarning(int, const char *, ...) {}
void Com_PrintError(int, const char *, ...) {}
void Com_Memset(void *destination, const int value, const size_t count)
{ std::memset(destination, value, count); }
void I_strncpyz(char *destination, const char *source, int size)
{ assert(size > 0); std::snprintf(destination, size, "%s", source); }
bool Sys_IsDatabaseThread() { return false; }
void Sys_Mkdir(const char *path)
{ assert(mkdir(Path(path).c_str(), 0700) == 0 || errno == EEXIST); }
int unzClose(unzFile) { std::abort(); }
int unzCloseCurrentFile(unzFile) { std::abort(); }
void Scr_SaveSourceImmediate(SaveImmediate *) {}
void SV_SaveDemoImmediate(SaveImmediate *) { std::abort(); }
void R_SaveGameThumbnail(const SaveHeader &) { ++thumbnails; }

FILE *FS_FileOpenReadBinary(const char *path)
{ return std::fopen(Path(path).c_str(), "rb"); }
FILE *FS_FileOpenWriteBinary(const char *path)
{ return std::fopen(Path(path).c_str(), "wb"); }
uint32_t FS_FileRead(void *buffer, uint32_t length, FILE *file)
{ return static_cast<uint32_t>(std::fread(buffer, 1, length, file)); }
uint32_t FS_FileWrite(const void *buffer, uint32_t length, FILE *file)
{ return refuseWrite ? 0 : static_cast<uint32_t>(std::fwrite(buffer, 1, length, file)); }
void FS_FileClose(FILE *file) { assert(std::fclose(file) == 0); }
int FS_FileGetFileSize(FILE *file)
{
    const long position = std::ftell(file);
    assert(std::fseek(file, 0, SEEK_END) == 0);
    const long length = std::ftell(file);
    assert(std::fseek(file, position, SEEK_SET) == 0);
    return static_cast<int>(length);
}
bool WebWorkerFS_Remove(const char *path)
{ return std::remove(Path(path).c_str()) == 0; }
bool WebWorkerFS_Rename(const char *from, const char *to)
{
    // Model the platform's admission refusal (for example queued-byte pressure).
    // A refused operation has not touched either file; C++ must not bypass it.
    return admitRename && std::rename(Path(from).c_str(), Path(to).c_str()) == 0;
}

static void Put(const char *path, const std::vector<uint8_t> &bytes)
{
    FILE *file = FS_FileOpenWriteBinary(path);
    assert(file);
    assert(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
    FS_FileClose(file);
}
static std::vector<uint8_t> Get(const char *path)
{
    FILE *file = FS_FileOpenReadBinary(path);
    assert(file);
    std::vector<uint8_t> bytes(FS_FileGetFileSize(file));
    assert(std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
    FS_FileClose(file);
    return bytes;
}
static bool Exists(const char *path)
{
    FILE *file = FS_FileOpenReadBinary(path);
    if (!file) return false;
    FS_FileClose(file);
    return true;
}

int main()
{
    home.current.string = "save-rename-tests";
    fs_homepath = &home;
    fs_debug = &debug;
    fs_searchpaths = &searchPath;
    std::strcpy(fs_gamedir, "main");
    Sys_Mkdir(home.current.string);
    Sys_Mkdir("save-rename-tests/main");
    Sys_Mkdir("save-rename-tests/main/save");
    Sys_Mkdir("save-rename-tests/players");
    Sys_Mkdir("save-rename-tests/players/profiles");
    Sys_Mkdir("save-rename-tests/players/profiles/test");
    Sys_Mkdir("save-rename-tests/players/profiles/test/save");
    const char *temporary = "save-rename-tests/main/save/temp.svg";
    const char *destination = "save-rename-tests/players/profiles/test/save/checkpoint.svg";
    char temporaryName[] = "save/temp.svg";
    char destinationName[] = "profiles/test/save/checkpoint.svg";
    char players[] = "players";
    const std::vector<uint8_t> oldBytes{1, 2, 3, 4};
    const std::vector<uint8_t> newBytes{5, 6, 7, 8};

    Put(destination, oldBytes);
    Put(temporary, newBytes);
    // Do not stub FS_Rename: this catches its former destructive fallback even
    // before considering the save writer's independently incorrect success flag.
    assert(!FS_Rename(temporaryName, fs_gamedir, destinationName, players));
    assert(Get(destination) == oldBytes);
    assert(Get(temporary) == newBytes);
    admitRename = true;
    assert(FS_Rename(temporaryName, fs_gamedir, destinationName, players));
    assert(Get(destination) == newBytes && !Exists(temporary));

    // Server downloads use the same admission boundary with home-relative paths.
    char serverTemporary[] = "main/save/temp.svg";
    char serverDestination[] = "players/profiles/test/save/checkpoint.svg";
    Put(destination, oldBytes);
    Put(temporary, newBytes);
    admitRename = false;
    FS_SV_Rename(serverTemporary, serverDestination);
    assert(Get(destination) == oldBytes && Get(temporary) == newBytes);
    admitRename = true;
    FS_SV_Rename(serverTemporary, serverDestination);
    assert(Get(destination) == newBytes && !Exists(temporary));

    SaveDevice_Init();
    SaveHeader header{};
    header.saveVersion = 287;
    header.bodySize = 4;
    std::strcpy(header.filename, destinationName);
    unsigned char body[]{9, 10, 11, 12};
    std::vector<uint8_t> savedBytes(sizeof(header) + sizeof(body));
    std::memcpy(savedBytes.data(), &header, sizeof(header));
    std::memcpy(savedBytes.data() + sizeof(header), body, sizeof(body));
    Put(destination, oldBytes);
    admitRename = false;
    assert(WriteSaveToDevice(body, &header, false) == -1);
    assert(!SaveDevice_IsSaveSuccessful());
    assert(Get(destination) == oldBytes && Get(temporary) == savedBytes);
    assert(thumbnails == 0);

    // Retry through the canonical save API after admission pressure clears.
    admitRename = true;
    assert(WriteSaveToDevice(body, &header, false) == 0);
    assert(SaveDevice_IsSaveSuccessful());
    assert(Get(destination) == savedBytes && !Exists(temporary));
    assert(thumbnails == 1);

    // A write failure still removes the incomplete temporary, never the old save.
    refuseWrite = true;
    assert(WriteSaveToDevice(body, &header, false) == -1);
    assert(!SaveDevice_IsSaveSuccessful());
    assert(Get(destination) == savedBytes && !Exists(temporary));
    assert(thumbnails == 1);
    std::puts("canonical save rename refusal, retry, and short-write handling passed");
}
