#include <gfx_d3d/r_rendercmds.h>
#include <universal/profile.h>

char __cdecl SetDrawText2DGlowParms(GfxCmdDrawText2D *cmd, const float *color, const float *glowColor)
{
    float scaledGlowColor[4]; // [esp+0h] [ebp-10h] BYREF

    if (!glowColor)
        return 0;
    if (glowColor[3] == 0.0)
        return 0;
    cmd->renderFlags |= 0x30u;
    scaledGlowColor[0] = *glowColor * 0.1000000014901161;
    scaledGlowColor[1] = glowColor[1] * 0.1000000014901161;
    scaledGlowColor[2] = glowColor[2] * 0.1000000014901161;
    scaledGlowColor[3] = glowColor[3] * color[3];
    Byte4PackVertexColor(scaledGlowColor, cmd->glowForceColor.array);
    return 1;
}

char __cdecl SetDrawText2DPulseFXParms(
    GfxCmdDrawText2D *cmd,
    Material *fxMaterial,
    Material *fxMaterialGlow,
    int fxBirthTime,
    int fxLetterTime,
    int fxDecayStartTime,
    int fxDecayDuration)
{
    if (!fxMaterial)
        return 0;
    if (!fxMaterialGlow)
        return 0;
    if (!fxBirthTime)
        return 0;
    cmd->renderFlags |= 0xC0u;
    cmd->fxMaterial = fxMaterial;
    cmd->fxMaterialGlow = fxMaterialGlow;
    cmd->fxBirthTime = fxBirthTime;
    cmd->fxLetterTime = fxLetterTime;
    cmd->fxDecayStartTime = fxDecayStartTime;
    cmd->fxDecayDuration = fxDecayDuration;
    cmd->padding = 0.0;
    return 1;
}

void __cdecl CopyPoolTextToCmd(char *textPool, int poolSize, int firstChar, int charCount, GfxCmdDrawText2D *cmd)
{
    uint32_t poolRemaining; // [esp+30h] [ebp-4h]

    iassert(cmd);
    
    PROF_SCOPED("R_memcpy");
    poolRemaining = poolSize - firstChar;

    if (charCount > poolSize - firstChar)
    {
        memcpy((uint8_t *)cmd->text, (uint8_t *)&textPool[firstChar], poolRemaining);
        memcpy((uint8_t *)&cmd->text[poolRemaining], (uint8_t *)textPool, charCount - poolRemaining);
    }
    else
    {
        memcpy((uint8_t *)cmd->text, (uint8_t *)&textPool[firstChar], charCount);
    }

    cmd->text[charCount] = 0;
}
