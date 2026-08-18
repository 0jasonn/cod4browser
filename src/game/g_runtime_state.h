#pragma once

// Narrow game-state query used by save-memory serialization. Keeping this
// query in the game owner prevents savememory.cpp from importing the entire
// renderer/cgame-heavy g_local.h graph.
int G_GetPlayerHealthPercentageForSave();
