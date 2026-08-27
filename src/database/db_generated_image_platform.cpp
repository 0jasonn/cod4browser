#include <universal/q_shared.h>
#include <database/db_generated_image_platform.h>

#include <cstdint>
#include <deque>
#include <limits>
#include <string>
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
    std::uint64_t generation = 0u;
};

struct RetainedImageKey
{
    std::string name;
    std::uint64_t generation;
};

std::unordered_map<std::string, RetainedImageLoadDef> g_imageLoadDefs;
std::deque<RetainedImageKey> g_imageLoadOrder;
std::size_t g_imageLoadDefBytes = 0u;
std::uint64_t g_imageLoadGeneration = 0u;
std::uint64_t g_imageLoadDefEvictionCount = 0u;

void EvictImageLoadDefs()
{
    while (g_imageLoadDefBytes > WEB_DB_IMAGE_PAYLOAD_BUDGET &&
        !g_imageLoadOrder.empty())
    {
        RetainedImageKey key = std::move(g_imageLoadOrder.front());
        g_imageLoadOrder.pop_front();
        const auto found = g_imageLoadDefs.find(key.name);
        if (found == g_imageLoadDefs.end() ||
            found->second.generation != key.generation)
        {
            continue;
        }
        g_imageLoadDefBytes -= found->second.data.size();
        g_imageLoadDefs.erase(found);
        ++g_imageLoadDefEvictionCount;
    }
}
} // namespace

void __cdecl Load_Texture(GfxTexture *remoteLoadDef, GfxImage *image)
{
    iassert(remoteLoadDef && image);
    iassert(remoteLoadDef->loadDef == image->texture.loadDef);
    const GfxImageLoadDef *loadDef = remoteLoadDef->loadDef;
    if (image->name && image->name[0] && loadDef &&
        loadDef->resourceSize >= 0 &&
        static_cast<std::uint64_t>(loadDef->resourceSize) <=
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        RetainedImageLoadDef retained;
        retained.levelCount = loadDef->levelCount;
        retained.flags = loadDef->flags;
        retained.dimensions[0] = loadDef->dimensions[0];
        retained.dimensions[1] = loadDef->dimensions[1];
        retained.dimensions[2] = loadDef->dimensions[2];
        retained.format = loadDef->format;
        retained.generation = ++g_imageLoadGeneration;
        retained.data.assign(loadDef->data,
            loadDef->data + static_cast<std::size_t>(loadDef->resourceSize));

        const std::string name(image->name);
        const auto existing = g_imageLoadDefs.find(name);
        if (existing != g_imageLoadDefs.end())
            g_imageLoadDefBytes -= existing->second.data.size();
        g_imageLoadDefBytes += retained.data.size();
        g_imageLoadDefs.insert_or_assign(name, std::move(retained));
        g_imageLoadOrder.push_back({name, g_imageLoadGeneration});
        EvictImageLoadDefs();
    }

    // Allocation block zero is transient DB scratch storage. Native IW3
    // consumes it into a D3D object here, so retaining this pointer in the
    // canonical union would expose recycled load-definition memory.
    image->texture.basemap = nullptr;
}

bool DB_WebGetImageLoadDef(const GfxImage *image, WebDbImageLoadDef &loadDef)
{
    if (!image || !image->name) return false;
    const auto found = g_imageLoadDefs.find(image->name);
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
        g_imageLoadDefEvictionCount,
    };
}

void DB_WebClearImageLoadDefs()
{
    g_imageLoadDefs.clear();
    g_imageLoadOrder.clear();
    g_imageLoadDefBytes = 0u;
}
