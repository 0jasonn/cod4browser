#include <universal/q_shared.h>
#include <ui/ui.h>
#include <client/client.h>
#include <universal/com_files.h>
#include <gfx_d3d/material_types.h>
#include <stringed/stringed_hooks.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <list>
#include <map>
#include <string>
#include <vector>

uiInfo_s uiInfo{};
ScreenPlacement scrPlaceFull{};
const dvar_t *ui_savegame = nullptr;
static std::map<std::string, std::vector<uint8_t>> files;
static std::vector<std::string> names;
static std::vector<const char *> listing;
static std::list<std::string> strings;
static const std::vector<uint8_t> *opened = nullptr;
static int closes, rawLookups, sorts;
static Material thumbnail{}, fallback{}, *ready = nullptr, *drawn = nullptr;
static std::string rawName, selected;
void MyAssertHandler(const char *, int, int, const char *, ...) { std::abort(); }
void I_strncpyz(char *dst, const char *src, int size)
{ assert(src && size > 0); std::snprintf(dst, size, "%s", src); }
int I_stricmp(const char *a, const char *b)
{
    while (*a && tolower(static_cast<unsigned char>(*a)) == tolower(static_cast<unsigned char>(*b))) { ++a; ++b; }
    return tolower(static_cast<unsigned char>(*a)) - tolower(static_cast<unsigned char>(*b));
}
int Com_sprintf(char *dst, uint32_t size, const char *format, ...)
{ va_list args; va_start(args, format); int n = vsnprintf(dst, size, format, args); va_end(args); return n; }
int Com_BuildPlayerProfilePath(char *dst, int size, const char *format, ...)
{
    char suffix[128]; va_list args; va_start(args, format);
    vsnprintf(suffix, sizeof(suffix), format, args); va_end(args);
    return snprintf(dst, size, "profiles/test/%s", suffix);
}
const char **FS_ListFiles(const char *path, const char *ext, FsListBehavior_e, int *count)
{
    assert(!strcmp(path, "profiles/test/save") && !strcmp(ext, "svg"));
    listing.clear(); for (auto &name : names) listing.push_back(name.c_str());
    *count = static_cast<int>(listing.size()); return listing.empty() ? nullptr : listing.data();
}
void FS_FreeFileList(const char **) {}
uint32_t FS_FOpenFileRead(const char *path, int *handle)
{
    assert(!opened);
    auto it = files.find(path); *handle = it != files.end();
    if (*handle) opened = &it->second;
    return opened ? static_cast<uint32_t>(opened->size()) : 0;
}
uint32_t FS_Read(uint8_t *buffer, uint32_t count, int handle)
{
    assert(handle && opened);
    count = std::min(count, static_cast<uint32_t>(opened->size()));
    memcpy(buffer, opened->data(), count); return count;
}
void FS_FCloseFile(int handle) { assert(handle && opened); opened = nullptr; ++closes; }
// These fixtures live in players, not fs_gamedir. The UI must use read search paths.
int FS_FileExists(char *) { return 0; }
const char *String_Alloc(const char *text) { strings.emplace_back(text); return strings.back().c_str(); }
void Dvar_SetString(dvar_s *, char *value) { selected = value; }
void UI_SavegameSort(int, int) { ++sorts; }
int UI_SavegameIndexFromFilename(const char *name)
{
    for (int i = 0; i < uiInfo.savegameCount; ++i)
        if (!strcmp(uiInfo.savegameList[uiInfo.savegameStatus.displaySavegames[i]].savegameFile, name)) return i;
    return -1;
}
Material *Material_RegisterRawImage(const char *name, int)
{ ++rawLookups; rawName = name; return ready; }
void Material_InvalidateRawImage() {}
char *SEH_LocalizeTextMessage(const char *text, const char *, msgLocErrType_t)
{ return const_cast<char *>(text); }
Material *Material_RegisterHandle(const char *name, int)
{ assert(!strcmp(name, "unknownsave")); return &fallback; }
void UI_DrawHandlePic(const ScreenPlacement *, float, float, float, float, int, int, const float *, Material *image)
{ drawn = image; }

static void Add(const std::string &name, const SaveHeader &header, size_t size = sizeof(SaveHeader) + 4)
{
    names.push_back(name);
    auto &data = files["profiles/test/save/" + name]; data.resize(size);
    memcpy(data.data(), &header, std::min(size, sizeof(header)));
}
int main()
{
    SaveHeader header{};
    header.saveVersion = 287; header.bodySize = 4;
    strcpy(header.description, "A synthetic checkpoint"); strcpy(header.mapName, "synthetic_map");
    strcpy(header.filename, "../../untrusted.svg"); strcpy(header.screenShotName, "../../untrusted.jpg");
    header.time.tm_year = 126; header.time.tm_mon = 8; header.time.tm_mday = 2;
    header.time.tm_hour = 14; header.time.tm_min = 7;
    Add("first.svg", header); Add("second.svg", header);
    files["profiles/test/save/second.jpg"] = {1, 2, 3};
    Add("short.svg", header, 16); Add("body.svg", header, sizeof(header));
    auto bad = header; bad.saveVersion = 286; Add("version.svg", bad);
    bad = header; bad.bodySize = -1; Add("negative.svg", bad);
    bad = header; memset(bad.description, 'X', sizeof(bad.description)); Add("description.svg", bad);
    bad = header; memset(bad.mapName, 'X', sizeof(bad.mapName)); Add("map.svg", bad);
    Add("../outside.svg", header); Add("bad\\path.svg", header); Add("bad.txt", header);
    Add(std::string(60, 'x') + ".svg", header);
    UI_LoadSavegames(0);
    assert(uiInfo.savegameCount == 2 && closes == 9 && sorts == 1 && !opened);
    const auto &save = uiInfo.savegameList[1];
    assert(!strcmp(save.savegameFile, "second") && !strcmp(save.savegameName, header.description));
    assert(!strcmp(save.mapName, "synthetic_map") && !strcmp(save.date, "2026-09-02") && !strcmp(save.time, "14:07"));
    assert(!strcmp(save.imageName, "profiles/test/save/second.jpg"));
    Material *handle = &thumbnail;
    assert(!strcmp(UI_FeederItemText(0, nullptr, 16, 1, 1, &handle), "2026-09-02") && !handle);
    assert(!strcmp(UI_FeederItemText(0, nullptr, 16, 1, 0, &handle), header.description));
    assert(!strcmp(UI_FeederItemText(0, nullptr, 16, 1, 2, &handle), "14:07"));
    assert(!*UI_FeederItemText(0, nullptr, 16, -1, 1, &handle));
    uiInfo.savegameStatus.displaySavegames[1] = 250;
    assert(!*UI_FeederItemText(0, nullptr, 16, 1, 1, &handle));
    uiInfo.savegameStatus.displaySavegames[1] = 1;
    strcpy(uiInfo.savegameName, "second");
    assert(!strcmp(UI_GetSavegameInfo(), header.description));
    strcpy(uiInfo.savegameName, "missing");
    strcpy(uiInfo.savegameInfo, "EXE_NOSAVEGAMES");
    assert(!strcmp(UI_GetSavegameInfo(), "EXE_NOSAVEGAMES"));
    rectDef_s rect{}; float color[4]{1, 1, 1, 1};
    strcpy(uiInfo.savegameName, "first"); UI_DrawSaveGameShot(&rect, 1, color);
    assert(drawn == &fallback && rawLookups == 0 && !uiInfo.sshotImage);
    strcpy(uiInfo.savegameName, "second"); UI_DrawSaveGameShot(&rect, 1, color);
    assert(drawn == &fallback && rawLookups == 1 && !uiInfo.sshotImageName[0]);
    ready = &thumbnail; UI_DrawSaveGameShot(&rect, 1, color);
    assert(drawn == &thumbnail && rawLookups == 2 && rawName == save.imageName);
    UI_DrawSaveGameShot(&rect, 1, color); assert(rawLookups == 2);
    files.erase("profiles/test/save/second.jpg");
    UI_SaveGameShotUpdated("profiles/other/save/second.jpg");
    assert(uiInfo.sshotImage == &thumbnail);
    UI_SaveGameShotUpdated("profiles/test/save/second.jpg");
    assert(!uiInfo.sshotImage && !uiInfo.savegameList[1].imageName);
    files["profiles/test/save/second.jpg"] = {4, 5, 6};
    UI_SaveGameShotUpdated("profiles/test/save/second.jpg");
    assert(!strcmp(uiInfo.savegameList[1].imageName, "profiles/test/save/second.jpg"));
    UI_LoadSavegames(0); assert(!uiInfo.sshotImage && !uiInfo.sshotImageName[0]);
    UI_DrawSaveGameShot(&rect, 1, color); assert(rawLookups == 3);
    files.clear(); names.clear(); strings.clear();
    bad = header; bad.time.tm_year = INT_MAX; Add("time.svg", bad);
    UI_LoadSavegames(0);
    assert(uiInfo.savegameCount == 1 && !uiInfo.savegameList[0].date && !uiInfo.savegameList[0].time);
    assert(uiInfo.savegameList[0].tm.tm_year == 0);
    files.clear(); names.clear(); strings.clear();
    for (int i = 0; i < 520; ++i) Add("save" + std::to_string(i) + ".svg", header);
    uiInfo.playerProfileCount = 31; uiInfo.timeIndex = 123456;
    UI_LoadSavegames(0);
    assert(uiInfo.savegameCount == 256 && uiInfo.timeIndex == 123456 && uiInfo.playerProfileCount == 31);
    for (int i = 0; i < 256; ++i) assert(uiInfo.savegameStatus.displaySavegames[i] == i);
    files.clear(); names.clear(); strings.clear(); UI_LoadSavegames(0);
    assert(!uiInfo.savegameCount && selected.empty() && !strcmp(uiInfo.savegameInfo, "EXE_NOSAVEGAMES"));
    UI_DrawSaveGameShot(&rect, 1, color); assert(drawn == &fallback);
    puts("canonical save menu: metadata, malformed headers, capacity, thumbnail selection/retry passed");
}
