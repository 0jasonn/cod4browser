#pragma once

// Renderer frontend statistics are consumed by cgame debugging but contain
// no backend objects.  Keep the ABI independent of the native D3D headers.
struct Image_MemUsage
{
    int total;
    int lightmap;
    int minspec;
};

struct trStatistics_t
{
    int c_indexes;
    int c_fxIndexes;
    int c_viewIndexes;
    int c_shadowIndexes;
    int c_vertexes;
    int c_batches;
    float dc;
    Image_MemUsage c_imageUsage;
};

void __cdecl R_TrackStatistics(trStatistics_t *stats);
