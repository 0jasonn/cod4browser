#pragma once

// Canonical single-player map-zone request. The map name is supplied by the
// server lifecycle; this owner contains no browser or test-selected name.
void SV_LoadLevelAssets(const char *mapname);
