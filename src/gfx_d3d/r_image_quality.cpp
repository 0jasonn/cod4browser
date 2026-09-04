#include <universal/q_shared.h>
#include "r_image_quality.h"
#include "r_dvars.h"
#include <qcommon/qcommon.h>

#if defined(KISAK_WEB)
// Native db_registry.cpp owns this canonical global on native targets.
ImgGlobals imageGlobals;
#endif

void R_SetPicmipForMemory(uint32_t texMemInMegs, uint32_t sysMemInMegs)
{
    bool cappedPicmip; // [esp+Bh] [ebp-5h]
    int minPicmip; // [esp+Ch] [ebp-4h]

    iassert( r_reflectionProbeGenerate );
    if (r_reflectionProbeGenerate->current.enabled)
    {
        Com_Printf(8, "Picmip is set to lowest quality for generating reflections.\n");
        imageGlobals.picmip = 2;
        imageGlobals.picmipBump = 2;
        imageGlobals.picmipSpec = 2;
    }
    else
    {
        if (r_picmip_manual->current.enabled)
        {
            Com_Printf(8, "Picmip is set manually.\n");
            imageGlobals.picmip = r_picmip->current.integer;
            imageGlobals.picmipBump = r_picmip_bump->current.integer;
            imageGlobals.picmipSpec = r_picmip_spec->current.integer;
        }
        else
        {
            Com_Printf(8, "Texture detail is set automatically.\n");
            if (texMemInMegs < 0x1C2)
            {
                if (texMemInMegs < 0x12C)
                {
                    imageGlobals.picmip = texMemInMegs < 0xC8;
                    imageGlobals.picmipBump = 1;
                }
                else
                {
                    imageGlobals.picmip = 0;
                    imageGlobals.picmipBump = 0;
                }
                imageGlobals.picmipSpec = 1;
            }
            else
            {
                imageGlobals.picmip = 0;
                imageGlobals.picmipBump = 0;
                imageGlobals.picmipSpec = 0;
            }
            if (sysMemInMegs > 0x180)
                minPicmip = sysMemInMegs <= 0x280;
            else
                minPicmip = 2;
            if (minPicmip)
            {
                cappedPicmip = 0;
                if (imageGlobals.picmip < minPicmip)
                {
                    imageGlobals.picmip = minPicmip;
                    cappedPicmip = 1;
                }
                if (imageGlobals.picmipBump < minPicmip)
                {
                    imageGlobals.picmipBump = minPicmip;
                    cappedPicmip = 1;
                }
                if (imageGlobals.picmipSpec < minPicmip)
                {
                    imageGlobals.picmipSpec = minPicmip;
                    cappedPicmip = 1;
                }
                if (cappedPicmip)
                    Com_Printf(
                        8,
                        "Reducing texture detail based on total system memory of %i MB to improve load times.\n",
                        sysMemInMegs);
            }
            Dvar_SetInt(r_picmip, imageGlobals.picmip);
            Dvar_SetInt(r_picmip_bump, imageGlobals.picmipBump);
            Dvar_SetInt(r_picmip_spec, imageGlobals.picmipSpec);
        }
        if (!r_specular->current.enabled || !r_rendererInUse->current.integer)
            imageGlobals.picmipSpec = 3;
        Com_Printf(
            8,
            "Using picmip %i on most textures, %i on normal maps, and %i on specular maps\n",
            imageGlobals.picmip,
            imageGlobals.picmipBump,
            imageGlobals.picmipSpec);
    }
}

void __cdecl Image_GetPicmip(const GfxImage *image, Picmip *picmip)
{
    iassert(image);
    iassert(picmip);

    if (image->noPicmip)
        *picmip = 0;
    else
        Image_PicmipForSemantic(image->semantic, picmip);
}

void __cdecl Image_PicmipForSemantic(uint8_t semantic, Picmip *picmip)
{
    int picmipUsed; // [esp+4h] [ebp-4h]

    switch (semantic)
    {
    case 0u:
    case 1u:
        goto $LN7_78;
    case 2u:
    case 0xBu:
        picmipUsed = imageGlobals.picmip;
        goto LABEL_8;
    case 5u:
        picmipUsed = imageGlobals.picmipBump;
        goto LABEL_8;
    case 8u:
        picmipUsed = imageGlobals.picmipSpec;
    LABEL_8:
        picmip->platform[1] = 2;
        if (picmipUsed >= 0)
        {
            if (picmipUsed > 3)
                picmipUsed = 3;
        }
        else
        {
            picmipUsed = 0;
        }
        picmip->platform[0] = picmipUsed;
        break;
    default:
        if (!alwaysfails)
        {
            MyAssertHandler(".\\r_image.cpp", 644, 1, va("unhandled case: %d", semantic));
        }
    $LN7_78:
        *picmip = 0;
        break;
    }
}

