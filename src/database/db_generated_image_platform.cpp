#include <universal/q_shared.h>
#include <database/db_generated_image_platform.h>

void __cdecl Load_Texture(GfxTexture *remoteLoadDef, GfxImage *image)
{
    iassert(remoteLoadDef && image);
    iassert(remoteLoadDef->loadDef == image->texture.loadDef);
    image->texture.basemap = nullptr;
}
