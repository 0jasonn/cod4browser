#pragma once
#include <bgame/weapon_types.h>
#include <universal/com_memory.h>

#define WP_NONE 0

enum playerWeaponFlags_t : __int32
{
    PWF_USING_OFFHAND = 0x2,
};

enum OffhandSecondaryClass : __int32
{
    PLAYER_OFFHAND_SECONDARY_SMOKE = 0x0,
    PLAYER_OFFHAND_SECONDARY_FLASH = 0x1,
    PLAYER_OFFHAND_SECONDARIES_TOTAL = 0x2,
};

enum weaponstate_t : __int32
{                                       // ...
    WEAPON_READY = 0x0,
    WEAPON_RAISING = 0x1,
    WEAPON_RAISING_ALTSWITCH = 0x2,
    WEAPON_DROPPING = 0x3,
    WEAPON_DROPPING_QUICK = 0x4,
    WEAPON_FIRING = 0x5,
    WEAPON_RECHAMBERING = 0x6,
    WEAPON_RELOADING = 0x7,
    WEAPON_RELOADING_INTERUPT = 0x8,
    WEAPON_RELOAD_START = 0x9,
    WEAPON_RELOAD_START_INTERUPT = 0xA,
    WEAPON_RELOAD_END = 0xB,
    WEAPON_MELEE_INIT = 0xC,
    WEAPON_MELEE_FIRE = 0xD,
    WEAPON_MELEE_END = 0xE,
    WEAPON_OFFHAND_INIT = 0xF,
    WEAPON_OFFHAND_PREPARE = 0x10,
    WEAPON_OFFHAND_HOLD = 0x11,
    WEAPON_OFFHAND_START = 0x12,
    WEAPON_OFFHAND = 0x13,
    WEAPON_OFFHAND_END = 0x14,
    WEAPON_DETONATING = 0x15,
    WEAPON_SPRINT_RAISE = 0x16,
    WEAPON_SPRINT_LOOP = 0x17,
    WEAPON_SPRINT_DROP = 0x18,
    WEAPON_NIGHTVISION_WEAR = 0x19,
    WEAPON_NIGHTVISION_REMOVE = 0x1A,
    WEAPONSTATES_NUM = 0x1B,
};

// bg_weapons_load_obj
struct SurfaceTypeSoundList // sizeof=0x8
{
    char *surfaceSoundBase;
    struct snd_alias_list_t **soundAliasList;
};

struct WeaponDef *__cdecl BG_LoadWeaponDef_LoadObj(const char *name);

inline void __cdecl SetConfigString(char **ppszConfigString, const char *pszKeyValue)
{
    char v2; // [esp+3h] [ebp-21h]
    char *v3; // [esp+8h] [ebp-1Ch]
    const char *v4; // [esp+Ch] [ebp-18h]
    char *buf; // [esp+20h] [ebp-4h]

    if (*pszKeyValue)
    {
        buf = (char *)Hunk_AllocLowAlign(strlen(pszKeyValue) + 1, 1, "SetConfigString", 9);
        v4 = pszKeyValue;
        v3 = buf;
        do
        {
            v2 = *v4;
            *v3++ = *v4++;
        } while (v2);
        *ppszConfigString = buf;
    }
    else
    {
        *ppszConfigString = (char *)"";
    }
}

inline void __cdecl SetConfigString2(uint8_t *pMember, const char *pszKeyValue)
{
    SetConfigString((char **)pMember, pszKeyValue);
}
