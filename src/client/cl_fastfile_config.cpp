#include <client/cl_fastfile_config.h>

#include <database/db_zone_loading.h>
#include <gfx_d3d/r_configuration.h>
#include <universal/assertive.h>

void CL_SetFastFileNames(GfxConfiguration *config, bool dedicatedServer)
{
    (void)dedicatedServer;
    iassert(config);

    config->codeFastFileName = "code_post_gfx";
    config->uiFastFileName = "ui";
    config->commonFastFileName = "common";
    config->localizedCodeFastFileName = nullptr;
    config->localizedCommonFastFileName = nullptr;
#if defined(KISAK_WEB)
    // Browser mod-zone discovery returns with the engine filesystem/mod path.
    config->modFastFileName = nullptr;
#else
    config->modFastFileName = DB_ModFileExists() ? "mod" : nullptr;
#endif
}
