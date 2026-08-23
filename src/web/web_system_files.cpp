#include <qcommon/system_files.h>

#include <universal/com_memory.h>
#include <universal/q_shared.h>
#include <web/web_worker_filesystem.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <array>

namespace
{
std::string JoinPath(const std::string &base, const char *name)
{
    if (base.empty() || base == ".")
        return name;
    return base + "/" + name;
}

bool HasExtension(const char *name, const char *extension)
{
    if (!extension || !*extension) return true;
    std::string expected = extension;
    if (expected.front() != '.') expected.insert(expected.begin(), '.');
    const char *dot = std::strrchr(name, '.');
    return dot && I_stricmp(dot, expected.c_str()) == 0;
}

char **CopyFileList(const std::vector<std::string> &names, int *numfiles)
{
    *numfiles = static_cast<int>(names.size());
    if (names.empty()) return nullptr;

    HunkUser *user = Hunk_UserCreate(0x20000, "Sys_ListFiles", false,
        false, 3);
    char **storage = static_cast<char **>(Hunk_UserAlloc(
        user, static_cast<std::uint32_t>((names.size() + 2) * sizeof(char *)),
        alignof(char *)));
    storage[0] = reinterpret_cast<char *>(user);
    char **list = storage + 1;
    for (std::size_t index = 0; index < names.size(); ++index)
        list[index] = Hunk_CopyString(user, names[index].c_str());
    list[names.size()] = nullptr;
    return list;
}

bool ListFilteredFiles(
    const std::string &directory,
    const std::string &relative,
    const char *filter,
    std::vector<std::string> &names)
{
    std::vector<WebWorkerDirectoryEntry> entries;
    const std::string path = relative.empty()
        ? directory
        : JoinPath(directory, relative.c_str());
    if (!WebWorkerFS_ListDirectory(path.c_str(), entries))
        return false;
    for (const WebWorkerDirectoryEntry &entry : entries)
    {
        const std::string child = relative.empty()
            ? entry.name
            : JoinPath(relative, entry.name);
        if (entry.type == WebWorkerFileType::Directory)
        {
            if (!ListFilteredFiles(directory, child, filter, names))
                return false;
        }
        else if (entry.type == WebWorkerFileType::File &&
            Com_FilterPath(filter, child.c_str(), 0))
        {
            names.push_back(child);
            if (names.size() == WEB_WORKER_MAX_DIRECTORY_ENTRIES)
                return true;
        }
    }
    return true;
}
}

char *__cdecl Sys_Cwd()
{
    // The Worker mount exposes a logical installation root, never a private
    // OPFS path. Canonical FS_BuildOSPath turns this into ./main/... and the
    // platform adapter normalizes that logical spelling at its boundary.
    static std::array<char, 2> currentDirectory{{'.', '\0'}};
    return currentDirectory.data();
}

const char *__cdecl Sys_DefaultCDPath()
{
    return "";
}

void __cdecl Sys_Mkdir(const char *path)
{
    if (path && *path)
        WebWorkerFS_Mkdir(path);
}

BOOL __cdecl Sys_RemoveDirTree(const char *path)
{
    (void)path;
    return 0;
}

int __cdecl Sys_CountFileList(char **list)
{
    int count = 0;
    while (list && list[count]) ++count;
    return count;
}

char **__cdecl Sys_ListFiles(const char *directory, const char *extension,
    const char *filter, int *numfiles, int wantsubs)
{
    iassert(numfiles);
    *numfiles = 0;
    if (!directory || !*directory) return nullptr;

    std::vector<std::string> names;
    if (filter && *filter)
    {
        if (!ListFilteredFiles(directory, "", filter, names))
            return nullptr;
    }
    else
    {
        const bool directoriesOnly = wantsubs ||
            (extension && extension[0] == '/' && extension[1] == '\0');
        std::vector<WebWorkerDirectoryEntry> entries;
        if (!WebWorkerFS_ListDirectory(directory, entries))
            return nullptr;
        for (const WebWorkerDirectoryEntry &entry : entries)
        {
            const bool isDirectory = entry.type == WebWorkerFileType::Directory;
            if (directoriesOnly != isDirectory) continue;
            if (!directoriesOnly && !HasExtension(entry.name, extension))
                continue;
            names.emplace_back(entry.name);
            if (names.size() == 0x1FFF) break;
        }
    }
    std::sort(names.begin(), names.end());
    return CopyFileList(names, numfiles);
}

int __cdecl Sys_DirectoryHasContents(const char *directory)
{
    if (!directory || !*directory) return 0;
    std::vector<WebWorkerDirectoryEntry> entries;
    return WebWorkerFS_ListDirectory(directory, entries) && !entries.empty();
}
