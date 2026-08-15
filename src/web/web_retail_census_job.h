#pragma once

#include <cstdint>

struct WebRetailCensusFrameResult
{
    std::uint32_t bytesUsed = 0u;
    std::uint32_t recordsUsed = 0u;
};

void WebRetailCensusJob_Start();
void WebRetailCensusJob_Cancel();
WebRetailCensusFrameResult WebRetailCensusJob_Frame();

extern "C" void KisakWeb_StartRetailCensus();
extern "C" void KisakWeb_CancelRetailCensus();
extern "C" int KisakWeb_SelectRetailXModel(std::uint32_t modelIndex);
