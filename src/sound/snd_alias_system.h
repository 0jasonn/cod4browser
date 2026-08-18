#pragma once

// Renderer- and driver-independent ownership of the sound-alias namespace
// used by qcommon, client, game, and server lifecycle code.
enum snd_alias_system_t : __int32
{
    SASYS_UI = 0x0,
    SASYS_CGAME = 0x1,
    SASYS_GAME = 0x2,
    SASYS_COUNT = 0x3,
};
