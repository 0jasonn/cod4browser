#pragma once

// Canonical IW3 database ABI shared by the native loader and portable zone
// traversal. Keep renderer/UI dependencies out of this serialized type.
struct LocalizeEntry // sizeof=0x8 on the 32-bit IW3 ABI
{
    const char *value;
    const char *name;
};

static_assert(sizeof(void *) != 4u || sizeof(LocalizeEntry) == 8u);
