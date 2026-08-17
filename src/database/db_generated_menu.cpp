#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_generated_menu_internal.h>
#include <database/db_runtime_prefix.h>
#include <ui/ui_asset_types.h>

#include <cstddef>
#include <cstdint>
#include <limits>

MenuList *varMenuList = nullptr;
MenuList **varMenuListPtr = nullptr;
menuDef_t *varmenuDef_t = nullptr;
menuDef_t **varmenuDefPtr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical menu loader requires the IW3 32-bit ABI");
static_assert(sizeof(windowDef_t) == 156u);
static_assert(sizeof(ItemKeyHandler) == 12u);
static_assert(sizeof(listBoxDef_s) == 340u);
static_assert(sizeof(editFieldDef_s) == 32u);
static_assert(sizeof(multiDef_s) == 392u);
static_assert(sizeof(itemDef_s) == 372u);
static_assert(sizeof(menuDef_t) == 284u);
static_assert(sizeof(MenuList) == 12u);

bool CheckedPointerTableBytes(std::int32_t count, const char *stage,
    std::size_t &bytes)
{
    if (count < 0 || static_cast<std::uint32_t>(count) >
        (std::numeric_limits<std::uint32_t>::max)() / sizeof(void *))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    bytes = static_cast<std::size_t>(count) * sizeof(void *);
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    return true;
}

void LoadWindow(windowDef_t *window, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(window),
        sizeof(*window));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &window->name;
    Load_XString(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &window->group;
    Load_XString(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varMaterialHandle = &window->background;
    Load_MaterialHandle(false);
}

void LoadItemKeyHandler(ItemKeyHandler *handler, bool atStreamStart)
{
    bool streamStart = atStreamStart;
    for (;;)
    {
        Load_Stream(streamStart, reinterpret_cast<std::uint8_t *>(handler),
            sizeof(*handler));
        if (DB_RuntimeGeneratedLoadFailed()) return;
        varXString = &handler->action;
        Load_XString(false);
        if (DB_RuntimeGeneratedLoadFailed() || !handler->next) return;
        handler->next = reinterpret_cast<ItemKeyHandler *>(
            AllocLoad_FxElemVisStateSample());
        handler = handler->next;
        streamStart = true;
    }
}

void LoadListBoxDef(listBoxDef_s *listBox, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(listBox),
        sizeof(*listBox));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &listBox->doubleClick;
    Load_XString(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varMaterialHandle = &listBox->selectIcon;
    Load_MaterialHandle(false);
}

void LoadMultiDef(multiDef_s *multi, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(multi),
        sizeof(*multi));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    for (const char **string = multi->dvarList;
        string != multi->dvarList + 32; ++string)
    {
        varXString = string;
        Load_XString(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
    for (const char **string = multi->dvarStr;
        string != multi->dvarStr + 32; ++string)
    {
        varXString = string;
        Load_XString(false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadItemTypeData(itemDef_s *item)
{
    if (!item->typeData.data) return;
    switch (item->type)
    {
    case 6:
        item->typeData.listBox = reinterpret_cast<listBoxDef_s *>(
            AllocLoad_FxElemVisStateSample());
        LoadListBoxDef(item->typeData.listBox, true);
        break;
    case 4:
    case 9:
    case 16:
    case 18:
    case 11:
    case 14:
    case 10:
    case 0:
    case 17:
        item->typeData.editField = reinterpret_cast<editFieldDef_s *>(
            AllocLoad_FxElemVisStateSample());
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(
            item->typeData.editField), sizeof(editFieldDef_s));
        break;
    case 12:
        item->typeData.multi = reinterpret_cast<multiDef_s *>(
            AllocLoad_FxElemVisStateSample());
        LoadMultiDef(item->typeData.multi, true);
        break;
    case 13:
        varXString = &item->typeData.enumDvarName;
        Load_XString(false);
        break;
    default:
        // Canonical generated code leaves unknown item type unions untouched.
        break;
    }
}

void LoadItem(itemDef_s *item, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(item),
        sizeof(*item));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    LoadWindow(&item->window, false);
    const char **strings[] = {
        &item->text, &item->mouseEnterText, &item->mouseExitText,
        &item->mouseEnter, &item->mouseExit, &item->action,
        &item->onAccept, &item->onFocus, &item->leaveFocus,
        &item->dvar, &item->dvarTest};
    for (const char **string : strings)
    {
        if (DB_RuntimeGeneratedLoadFailed()) return;
        varXString = string;
        Load_XString(false);
    }
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (item->onKey)
    {
        item->onKey = reinterpret_cast<ItemKeyHandler *>(
            AllocLoad_FxElemVisStateSample());
        LoadItemKeyHandler(item->onKey, true);
    }
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varXString = &item->enableDvar;
    Load_XString(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    varsnd_alias_list_ptr = &item->focusSound;
    Load_snd_alias_list_ptr(false);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    LoadItemTypeData(item);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    statement_s *statements[] = {
        &item->visibleExp, &item->textExp, &item->materialExp,
        &item->rectXExp, &item->rectYExp, &item->rectWExp,
        &item->rectHExp, &item->forecolorAExp};
    for (statement_s *statement : statements)
    {
        DB_LoadGeneratedStatement(statement, false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadItemPointerArray(itemDef_s ***destination, std::int32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedPointerTableBytes(count, "Menu/item pointer table", bytes))
        return;
    *destination = reinterpret_cast<itemDef_s **>(
        AllocLoad_FxElemVisStateSample());
    itemDef_s **items = *destination;
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(items),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    for (std::int32_t index = 0; index < count; ++index)
    {
        if (!items[index]) continue;
        items[index] = reinterpret_cast<itemDef_s *>(
            AllocLoad_FxElemVisStateSample());
        LoadItem(items[index], true);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadMenu(menuDef_t *menu, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(menu),
        sizeof(*menu));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    LoadWindow(&menu->window, false);
    const char **strings[] = {
        &menu->font, &menu->onOpen, &menu->onClose, &menu->onESC};
    for (const char **string : strings)
    {
        if (DB_RuntimeGeneratedLoadFailed()) break;
        varXString = string;
        Load_XString(false);
    }
    if (!DB_RuntimeGeneratedLoadFailed() && menu->onKey)
    {
        menu->onKey = reinterpret_cast<ItemKeyHandler *>(
            AllocLoad_FxElemVisStateSample());
        LoadItemKeyHandler(menu->onKey, true);
    }
    if (!DB_RuntimeGeneratedLoadFailed())
        DB_LoadGeneratedStatement(&menu->visibleExp, false);
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varXString = &menu->allowedBinding;
        Load_XString(false);
    }
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        varXString = &menu->soundName;
        Load_XString(false);
    }
    if (!DB_RuntimeGeneratedLoadFailed())
        DB_LoadGeneratedStatement(&menu->rectXExp, false);
    if (!DB_RuntimeGeneratedLoadFailed())
        DB_LoadGeneratedStatement(&menu->rectYExp, false);
    if (!DB_RuntimeGeneratedLoadFailed() && menu->items)
        LoadItemPointerArray(&menu->items, menu->itemCount);
    DB_PopStreamPos();
}

void LoadMenuPointer(menuDef_t **pointer, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(pointer), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*pointer)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(*pointer);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *pointer = reinterpret_cast<menuDef_t *>(
                AllocLoad_FxElemVisStateSample());
            varmenuDef_t = *pointer;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            LoadMenu(*pointer, true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_MenuAsset(reinterpret_cast<XAssetHeader *>(pointer));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*pointer)->window.name);
                if (inserted) *inserted = *pointer;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(pointer));
        }
    }
    DB_PopStreamPos();
}

void LoadMenuPointerArray(menuDef_t ***destination, std::int32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedPointerTableBytes(count, "MenuList/menu pointer table", bytes))
        return;
    *destination = reinterpret_cast<menuDef_t **>(
        AllocLoad_FxElemVisStateSample());
    menuDef_t **menus = *destination;
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(menus),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    for (std::int32_t index = 0; index < count; ++index)
    {
        LoadMenuPointer(&menus[index], false);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void LoadMenuList(MenuList *menuList, bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(menuList),
        sizeof(*menuList));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &menuList->name;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed() && menuList->menus)
        LoadMenuPointerArray(&menuList->menus, menuList->menuCount);
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_MenuListPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, reinterpret_cast<std::uint8_t *>(varMenuListPtr),
        4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varMenuListPtr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varMenuListPtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varMenuListPtr = reinterpret_cast<MenuList *>(
                AllocLoad_FxElemVisStateSample());
            varMenuList = *varMenuListPtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            LoadMenuList(*varMenuListPtr, true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_MenuListAsset(reinterpret_cast<XAssetHeader *>(
                    varMenuListPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varMenuListPtr)->name);
                if (inserted) *inserted = *varMenuListPtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varMenuListPtr));
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_menuDef_ptr(bool atStreamStart)
{
    LoadMenuPointer(varmenuDefPtr, atStreamStart);
}
