#pragma once

struct GfxConfiguration;

// Shared renderer-owned construction of the startup database request. The
// platform may supply the configuration, but it does not own zone ordering.
void R_LoadGraphicsAssetZones(const GfxConfiguration &config);
