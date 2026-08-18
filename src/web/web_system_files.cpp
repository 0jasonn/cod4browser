#include <qcommon/system_files.h>

#include <universal/com_memory.h>
#include <universal/q_shared.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include <array>

namespace
{
namespace fs = std::filesystem;

std::string NormalizePath(const fs::path &path)
{
    return path.generic_string();
}

bool HasExtension(const fs::path &path, const char *extension)
{
    if (!extension || !*extension) return true;
    std::string expected = extension;
    if (expected.front() != '.') expected.insert(expected.begin(), '.');
    return I_stricmp(path.extension().string().c_str(), expected.c_str()) == 0;
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
}

char *__cdecl Sys_Cwd()
{
    static std::array<char, 1024> currentDirectory{};
    const std::string path = NormalizePath(fs::current_path());
    I_strncpyz(currentDirectory.data(), path.c_str(),
        static_cast<int>(currentDirectory.size()));
    return currentDirectory.data();
}

const char *__cdecl Sys_DefaultCDPath()
{
    return "";
}

void __cdecl Sys_Mkdir(const char *path)
{
    if (path && *path) std::filesystem::create_directory(path);
}

BOOL __cdecl Sys_RemoveDirTree(const char *path)
{
    if (!path || !*path) return 0;
    std::error_code error;
    std::filesystem::remove_all(path, error);
    return error ? 0 : 1;
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
    std::error_code error;
    const fs::path base(directory);
    if (filter && *filter)
    {
        for (fs::recursive_directory_iterator iterator(base, error), end;
             !error && iterator != end; iterator.increment(error))
        {
            if (!iterator->is_regular_file(error)) continue;
            const std::string relative = NormalizePath(
                fs::relative(iterator->path(), base, error));
            if (!error && Com_FilterPath(filter, relative.c_str(), 0))
                names.push_back(relative);
        }
    }
    else
    {
        const bool directoriesOnly = wantsubs ||
            (extension && extension[0] == '/' && extension[1] == '\0');
        for (fs::directory_iterator iterator(base, error), end;
             !error && iterator != end; iterator.increment(error))
        {
            const bool isDirectory = iterator->is_directory(error);
            if (directoriesOnly != isDirectory) continue;
            if (!directoriesOnly && !HasExtension(iterator->path(), extension))
                continue;
            names.push_back(iterator->path().filename().string());
            if (names.size() == 0x1FFF) break;
        }
    }
    std::sort(names.begin(), names.end());
    return CopyFileList(names, numfiles);
}

int __cdecl Sys_DirectoryHasContents(const char *directory)
{
    if (!directory || !*directory) return 0;
    std::error_code error;
    return std::filesystem::directory_iterator(directory, error) !=
        std::filesystem::directory_iterator{};
}
