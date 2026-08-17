#pragma once

// Canonical IW3/Kisak PhysPreset ABI shared by generated DB loading and xanim.
struct PhysPreset
{
    const char *name;
    int type;
    float mass;
    float bounce;
    float friction;
    float bulletForceScale;
    float explosiveForceScale;
    const char *sndAliasPrefix;
    float piecesSpreadFraction;
    float piecesUpwardVelocity;
    bool tempDefaultToCylinder;
    unsigned char padding[3];
};

static_assert(sizeof(void *) != 4 || sizeof(PhysPreset) == 44);
