#include <universal/q_shared.h>
#include <database/db_generated_image_platform.h>
#include <database/database.h>
#include <database/db_registry_pools.h>
#include <database/db_runtime_prefix.h>

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t WEB_DB_IMAGE_PAYLOAD_BUDGET = 256u * 1024u * 1024u;

struct RetainedImageLoadDef
{
    std::uint8_t levelCount = 0u;
    std::uint8_t flags = 0u;
    std::int16_t dimensions[3]{};
    GfxImageFormat format = 0;
    std::vector<std::uint8_t> data;
    bool referenced = false;
};

std::unordered_map<std::uintptr_t, RetainedImageLoadDef> g_imageLoadDefs;
std::size_t g_imageLoadDefBytes = 0u;
std::uint64_t g_imageLoadGeneration = 0u;
} // namespace

void __cdecl Load_Texture(GfxTexture *remoteLoadDef, GfxImage *image)
{
    iassert(remoteLoadDef && image);
    iassert(remoteLoadDef->loadDef == image->texture.loadDef);
    const GfxImageLoadDef *loadDef = remoteLoadDef->loadDef;
    image->texture.webResource = 0;
    if (image->name && image->name[0] && loadDef &&
        loadDef->resourceSize >= 0 &&
        static_cast<std::uint64_t>(loadDef->resourceSize) <=
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        // A speculative load must not evict a surviving image's only source.
        // Admit before copying; normal completion/unload collects dead handles.
        if (static_cast<std::size_t>(loadDef->resourceSize) >
            WEB_DB_IMAGE_PAYLOAD_BUDGET - g_imageLoadDefBytes)
        {
            DB_RuntimeGeneratedFailure("image/retained source budget exceeded");
            return;
        }
        // Never reuse a handle, including after collection or explicit clear:
        // an old canonical copy must not resolve to another resource.
        if (g_imageLoadGeneration >= UINTPTR_MAX - 2u)
        {
            DB_RuntimeGeneratedFailure("image/resource handle exhaustion");
            return;
        }
        const auto handle = static_cast<std::uintptr_t>(++g_imageLoadGeneration);
        RetainedImageLoadDef retained;
        retained.levelCount = loadDef->levelCount;
        retained.flags = loadDef->flags;
        retained.dimensions[0] = loadDef->dimensions[0];
        retained.dimensions[1] = loadDef->dimensions[1];
        retained.dimensions[2] = loadDef->dimensions[2];
        retained.format = loadDef->format;
        retained.data.assign(loadDef->data,
            loadDef->data + static_cast<std::size_t>(loadDef->resourceSize));

        g_imageLoadDefBytes += retained.data.size();
        g_imageLoadDefs.emplace(handle, std::move(retained));
        image->texture.webResource = handle;
    }

    // Allocation block zero is transient DB scratch storage. Native IW3
    // consumes it into a D3D object here. Only the platform resource handle
    // remains in the canonical union, never the recycled load-definition pointer.
}

bool DB_WebGetImageLoadDef(const GfxImage *image, WebDbImageLoadDef &loadDef)
{
    if (!image) return false;
    const auto found = g_imageLoadDefs.find(image->texture.webResource);
    if (found == g_imageLoadDefs.end()) return false;
    const RetainedImageLoadDef &retained = found->second;
    loadDef.levelCount = retained.levelCount;
    loadDef.flags = retained.flags;
    loadDef.dimensions[0] = retained.dimensions[0];
    loadDef.dimensions[1] = retained.dimensions[1];
    loadDef.dimensions[2] = retained.dimensions[2];
    loadDef.format = retained.format;
    loadDef.data = retained.data.data();
    loadDef.byteLength = retained.data.size();
    return true;
}

WebDbImageLoadDefStats DB_WebGetImageLoadDefStats()
{
    return {
        g_imageLoadDefs.size(),
        g_imageLoadDefBytes,
        WEB_DB_IMAGE_PAYLOAD_BUDGET,
        0u, // Retained for telemetry compatibility; live resources are not evicted.
    };
}

void DB_WebClearImageLoadDefs()
{
    g_imageLoadDefs.clear();
    g_imageLoadDefBytes = 0u;
}

void DB_WebReleaseUnusedImageLoadDefs()
{
    // Synchronous DB completion/unload boundary: canonical primary and
    // override entries are the sole owners. Copies of a default can share a
    // resource, so collecting by zone or name would release live bytes.
    for (auto &[handle, retained] : g_imageLoadDefs) retained.referenced = false;
    for (std::uint32_t hash = 0; hash < 0x8000u; ++hash)
        for (std::uint32_t index = db_hashTable[hash]; index;
             index = g_assetEntryPool[index].entry.nextHash)
        {
            if (g_assetEntryPool[index].entry.asset.type != ASSET_TYPE_IMAGE) continue;
            for (std::uint32_t current = index; current;
                 current = g_assetEntryPool[current].entry.nextOverride)
            {
                const GfxImage *image = g_assetEntryPool[current].entry.asset.header.image;
                const auto found = g_imageLoadDefs.find(image->texture.webResource);
                if (found != g_imageLoadDefs.end()) found->second.referenced = true;
            }
        }
    std::erase_if(g_imageLoadDefs, [](const auto &entry) {
        if (entry.second.referenced) return false;
        g_imageLoadDefBytes -= entry.second.data.size();
        return true;
    });
}
