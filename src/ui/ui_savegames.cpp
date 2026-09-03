#include <universal/q_shared.h>
#include "ui.h"
#include <client/client.h>
#include <gfx_d3d/r_material.h>
#include <qcommon/com_playerprofile.h>
#include <universal/com_files.h>
#include <iterator>
#include <stringed/stringed_hooks.h>

const char *__cdecl UI_FeederItemText(int, itemDef_s *, const float feederID,
    int index, unsigned int column, Material **handle)
{
    *handle = nullptr;
    if (feederID == 16.0f)
    {
        if (index < 0 || index >= uiInfo.savegameCount) return "";
        const int slot = uiInfo.savegameStatus.displaySavegames[index];
        if (slot < 0 || slot >= uiInfo.savegameCount) return "";
        const SavegameInfo &save = uiInfo.savegameList[slot];
        // The shipped PC menu labels its two columns Name and Date.
        const char *text = column == 1 ? save.date : column == 2 ? save.time :
            column == 3 ? save.mapName : save.savegameName;
        return text ? text : "";
    }
    if (feederID == 24.0f)
    {
        if (index < 0 || index >= uiInfo.playerProfileCount) return "";
        const int slot = uiInfo.playerProfileStatus.displayProfile[index];
        if (slot < 0 || slot >= uiInfo.playerProfileCount) return "";
        return uiInfo.playerProfileName[slot] ? uiInfo.playerProfileName[slot] : "";
    }
    return "";
}

const char *__cdecl UI_GetSavegameInfo()
{
    const int display = UI_SavegameIndexFromFilename(uiInfo.savegameName);
    const int slot = display >= 0 && display < uiInfo.savegameCount
        ? uiInfo.savegameStatus.displaySavegames[display] : -1;
    const char *text = slot >= 0 && slot < uiInfo.savegameCount
        ? uiInfo.savegameList[slot].savegameInfoText : uiInfo.savegameInfo;
    return SEH_LocalizeTextMessage(text ? text : "", "Savegame Description Text", LOCMSG_SAFE);
}

static bool UI_SaveImageExists(const char *path)
{
    // FS_FileExists only searches fs_gamedir. Profile saves live in players.
    int file = 0;
    FS_FOpenFileRead(path, &file);
    if (!file) return false;
    FS_FCloseFile(file);
    return true;
}

void UI_InvalidateSaveGameShot()
{
    Material_InvalidateRawImage();
    uiInfo.sshotImage = nullptr;
    uiInfo.sshotImageName[0] = 0;
}

void UI_SaveGameShotUpdated(const char *path)
{
    // A codec completion can arrive after a profile switch or menu opening.
    // Update only entries belonging to the current canonical UI/profile.
    for (int i = 0; i < uiInfo.savegameCount; ++i)
    {
        SavegameInfo &save = uiInfo.savegameList[i];
        char expected[64];
        const int size = Com_BuildPlayerProfilePath(expected, sizeof(expected),
            "save/%s.jpg", save.savegameFile);
        if (size < 0 || size >= sizeof(expected) || I_stricmp(path, expected)) continue;
        save.imageName = UI_SaveImageExists(expected) ? String_Alloc(expected) : nullptr;
        UI_InvalidateSaveGameShot();
        break;
    }
}

void __cdecl UI_LoadSavegames(int /*unused*/)
{
    UI_InvalidateSaveGameShot();
    char saveDir[64];
#ifdef KISAK_XBOX
    I_strncpyz(saveDir, "save", sizeof(saveDir));
#else
    Com_BuildPlayerProfilePath(saveDir, sizeof(saveDir), "save");
#endif
    int saveCount = 0;
    const char **saveFiles = FS_ListFiles(saveDir, "svg", FS_LIST_ALL, &saveCount);
    uiInfo.savegameCount = 0;
    if (saveFiles)
    {
        // The display index has fewer entries than the backing save array.
        for (int i = 0; i < saveCount &&
            uiInfo.savegameCount < std::size(uiInfo.savegameStatus.displaySavegames); ++i)
        {
            const char *fileName = saveFiles[i];
            if (!fileName) continue;
            const size_t len = strlen(fileName);
            if (len <= 4 || len >= 64 || I_stricmp(fileName + len - 4, ".svg") ||
                strstr(fileName, "..") || strchr(fileName, '\\') || strchr(fileName, ':') ||
                fileName[0] == '/') continue;

            // Derive paths from the active profile and the directory entry,
            // never from filenames embedded in an untrusted save header.
            char path[64];
            const int pathSize = Com_sprintf(path, sizeof(path), "%s/%s", saveDir, fileName);
            if (pathSize < 0 || pathSize >= sizeof(path)) continue;
            int handle = 0;
            const unsigned size = FS_FOpenFileRead(path, &handle);
            if (!handle) continue;
            SaveHeader header{};
            const unsigned read = FS_Read(reinterpret_cast<uint8_t *>(&header), sizeof(header), handle);
            FS_FCloseFile(handle);
            if (read != sizeof(header) || size < sizeof(header) || header.saveVersion != 287 ||
                header.bodySize < 0 || static_cast<unsigned>(header.bodySize) > size - sizeof(header) ||
                !memchr(header.description, 0, sizeof(header.description)) ||
                !memchr(header.mapName, 0, sizeof(header.mapName))) continue;

            char name[64];
            I_strncpyz(name, fileName, sizeof(name));
            name[len - 4] = 0;
            const int index = uiInfo.savegameCount++;
            SavegameInfo &save = uiInfo.savegameList[index];
            save = {};
            save.savegameFile = String_Alloc(name);
            save.savegameName = String_Alloc(header.description[0] ? header.description : name);
            save.mapName = String_Alloc(header.mapName);
            save.savegameInfoText = String_Alloc(header.description);
            save.tm = header.time;
            // Keep malformed timestamps out of formatting and time sorting.
            if (save.tm.tm_year >= 70 && save.tm.tm_year <= 8099 &&
                save.tm.tm_mon >= 0 && save.tm.tm_mon <= 11 &&
                save.tm.tm_mday >= 1 && save.tm.tm_mday <= 31 &&
                save.tm.tm_hour >= 0 && save.tm.tm_hour <= 23 &&
                save.tm.tm_min >= 0 && save.tm.tm_min <= 59 &&
                save.tm.tm_sec >= 0 && save.tm.tm_sec <= 60)
            {
                char text[32];
                Com_sprintf(text, sizeof(text), "%04d-%02d-%02d", save.tm.tm_year + 1900,
                    save.tm.tm_mon + 1, save.tm.tm_mday);
                save.date = String_Alloc(text);
                Com_sprintf(text, sizeof(text), "%02d:%02d", save.tm.tm_hour, save.tm.tm_min);
                save.time = String_Alloc(text);
            }
            else save.tm = {};
            // PC deletion already pairs .svg with .jpg. screenShotName is
            // authored save metadata and is not a trusted filesystem path.
            memcpy(path + strlen(path) - 3, "jpg", 3);
            if (UI_SaveImageExists(path)) save.imageName = String_Alloc(path);
            uiInfo.savegameStatus.displaySavegames[index] = index;
        }
        FS_FreeFileList(saveFiles);
    }
    if (uiInfo.savegameCount)
    {
        uiInfo.savegameStatus.sortDir = 1;
        UI_SavegameSort(uiInfo.savegameStatus.sortKey, 1);
    }
    else
    {
        Dvar_SetString(ui_savegame, "");
        uiInfo.savegameName[0] = 0;
        strcpy(uiInfo.savegameInfo, "EXE_NOSAVEGAMES");
    }
}

void __cdecl UI_DrawSaveGameShot(rectDef_s *rect, double /*scale*/, float *color)
{
    const int display = UI_SavegameIndexFromFilename(uiInfo.savegameName);
    const int slot = display >= 0 && display < uiInfo.savegameCount
        ? uiInfo.savegameStatus.displaySavegames[display] : -1;
    const char *name = slot >= 0 && slot < uiInfo.savegameCount
        ? uiInfo.savegameList[slot].imageName : nullptr;
    Material *image = nullptr;
    if (name && *name)
    {
        if (uiInfo.sshotImage && !I_stricmp(name, uiInfo.sshotImageName))
            image = uiInfo.sshotImage;
        else image = Material_RegisterRawImage(name, 3);
    }
    uiInfo.sshotImage = image;
    // A missing or pending platform image must remain eligible for retry.
    I_strncpyz(uiInfo.sshotImageName, image ? name : "", sizeof(uiInfo.sshotImageName));
    if (!image) image = Material_RegisterHandle("unknownsave", 3);
    UI_DrawHandlePic(&scrPlaceFull, rect->x, rect->y, rect->w, rect->h,
        rect->horzAlign, rect->vertAlign, color, image);
}
