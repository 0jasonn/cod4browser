#include <universal/q_shared.h>
#include <qcommon/qcommon_math.h>
#include "r_gamma.h"
#include "r_dvars.h"

void __cdecl R_CalcGammaRamp(GfxGammaRamp *gammaRamp)
{
    float unitScaleValue; // [esp+8h] [ebp-30h]
    float v2; // [esp+Ch] [ebp-2Ch]
    uint16_t adjustedColorValue; // [esp+28h] [ebp-10h]
    uint16_t colorTableIndex; // [esp+2Ch] [ebp-Ch]
    float exponent; // [esp+30h] [ebp-8h]

    iassert( gammaRamp );
    iassert(r_gamma->current.value > 0);

    exponent = 1.0 / r_gamma->current.value;
    for (colorTableIndex = 0; colorTableIndex < 0x100u; ++colorTableIndex)
    {
        if (exponent == 1.0)
        {
            adjustedColorValue = 257 * colorTableIndex;
        }
        else
        {
            v2 = (double)colorTableIndex / 255.0;
            unitScaleValue = pow(v2, exponent);
            iassert(unitScaleValue >= 0 && unitScaleValue < 1 + 0.5f / 65535);
            adjustedColorValue = SnapFloatToInt(unitScaleValue * 65535.0f);
        }
        gammaRamp->entries[colorTableIndex] = adjustedColorValue;
    }
}

void __cdecl R_GammaCorrect(uint8_t *buffer, int bufSize)
{
    int tableIndex; // [esp+0h] [ebp-210h]
    GfxGammaRamp gammaRamp; // [esp+8h] [ebp-208h] BYREF
    int inValue; // [esp+20Ch] [ebp-4h]

    iassert( buffer );
    iassert( (bufSize > 0) );
    R_CalcGammaRamp(&gammaRamp);
    for (tableIndex = 0; tableIndex < bufSize; ++tableIndex)
    {
        inValue = buffer[tableIndex];
        buffer[tableIndex] = 255 * gammaRamp.entries[inValue] / 0xFFFF;
    }
}

