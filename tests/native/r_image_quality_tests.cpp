#include <universal/q_shared.h>
#include <gfx_d3d/r_image_quality.h>
#include <gfx_d3d/r_dvars.h>
#include <qcommon/qcommon.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

dvar_t manual{}, color{}, bump{}, spec{}, reflection{}, specular{}, renderer{};
const dvar_t *r_picmip_manual = &manual, *r_picmip = &color,
    *r_picmip_bump = &bump, *r_picmip_spec = &spec,
    *r_reflectionProbeGenerate = &reflection, *r_specular = &specular,
    *r_rendererInUse = &renderer;
void MyAssertHandler(const char *, int, int, const char *, ...) { std::abort(); }
char *va(const char *, ...) { std::abort(); }
void Com_Printf(int, const char *, ...) {}
void Dvar_SetInt(dvar_t *dvar, int value) { dvar->current.integer = value; }

void Check(int c, int b, int s)
{
    assert(imageGlobals.picmip == c && imageGlobals.picmipBump == b &&
        imageGlobals.picmipSpec == s);
}

int main()
{
    specular.current.enabled = true;
    renderer.current.integer = 1;
    struct Case { unsigned gpu, system; int c, b, s; };
    for (const Case test : {Case{199, 1024, 1, 1, 1}, {200, 1024, 0, 1, 1},
        {299, 1024, 0, 1, 1}, {300, 1024, 0, 0, 1}, {449, 1024, 0, 0, 1},
        {450, 1024, 0, 0, 0}, {800, 384, 2, 2, 2}, {800, 385, 1, 1, 1},
        {800, 640, 1, 1, 1}, {800, 641, 0, 0, 0}})
    {
        R_SetPicmipForMemory(test.gpu, test.system);
        Check(test.c, test.b, test.s);
        assert(color.current.integer == test.c && bump.current.integer == test.b &&
            spec.current.integer == test.s);
    }
    manual.current.enabled = true;
    color.current.integer = 1; bump.current.integer = 2; spec.current.integer = 3;
    R_SetPicmipForMemory(1, 1); Check(1, 2, 3);
    for (unsigned semantic : {0u, 1u, 2u, 5u, 8u, 11u})
    {
        GfxImage image{};
        image.semantic = semantic;
        Picmip mip{};
        Image_GetPicmip(&image, &mip);
        assert(mip.platform[0] == (semantic < 2 ? 0 : semantic == 5 ? 2 : semantic == 8 ? 3 : 1));
        assert(mip.platform[1] == (semantic < 2 ? 0 : 2));
        image.noPicmip = true;
        Image_GetPicmip(&image, &mip);
        assert(mip.platform[0] == 0 && mip.platform[1] == 0);
    }
    for (int value : {-1, 0, 1, 2, 3, 4})
    {
        imageGlobals.picmip = value;
        Picmip mip{};
        Image_PicmipForSemantic(2, &mip);
        assert(mip.platform[0] == (value < 0 ? 0 : value > 3 ? 3 : value));
    }
    spec.current.integer = 0;
    specular.current.enabled = false;
    R_SetPicmipForMemory(800, 1024); Check(1, 2, 3);
    specular.current.enabled = true;
    renderer.current.integer = 0;
    R_SetPicmipForMemory(800, 1024); Check(1, 2, 3);
    reflection.current.enabled = true;
    R_SetPicmipForMemory(800, 1024); Check(2, 2, 2);
    std::puts("native image quality: memory thresholds, manual, semantics, exemptions, overrides passed");
}
