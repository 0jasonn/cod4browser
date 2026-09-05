// Uses the existing GPL-3.0 synthetic XFile fixtures and host service stubs.
// Native links original db_load.cpp; generated links the adapted loader TUs.
#if defined(KISAK_NATIVE_DB_LOAD_ASSET_ORACLE)
void DB_SetGeneratedAssetIndex(std::uint32_t) {}
// No live DObjs, MP world, sound or world vertex-buffer asset is part of this
// fixture. Keep native entry points linkable and fail if it ever enters one.
struct DObj_s;
struct MssSoundCOD4;
struct IDirect3DVertexBuffer9;
DObj_s *Com_GetClientDObj(unsigned int, int) { return nullptr; }
DObj_s *Com_GetServerDObj(unsigned int) { return nullptr; }
void DObjArchive(DObj_s *) { std::abort(); }
void DObjUnarchive(DObj_s *) { std::abort(); }
void Load_GameWorldMpAsset(XAssetHeader *) { std::abort(); }
void Mark_GameWorldMpAsset(GameWorldMp *) { std::abort(); }
void SND_SetData(MssSoundCOD4 *, void *) { std::abort(); }
void Load_VertexBuffer(IDirect3DVertexBuffer9 **, unsigned char *, int) { std::abort(); }
#endif

int TestIndependentAssetOracle()
{
    extern PhysicalMemory g_mem;
    PMem_InitPhysicalMemory(&g_mem, g_arena.data(), static_cast<std::uint32_t>(g_arena.size()));
    Sys_InitMainThread();
    DB_InitThread();
    g_file = MakeMaterialXFile();
    XZoneInfo request{"material-oracle", 8, 0};
    DB_LoadXAssets(&request, 1, 1);
    assert(Sys_IsDatabaseReady() && !DB_RuntimeGeneratedLoadFailed());
    const auto *material = DB_FindXAssetHeader(ASSET_TYPE_MATERIAL, "materials/gate3").material;
    const auto *image = DB_FindXAssetHeader(ASSET_TYPE_IMAGE, "images/gate3").image;
    assert(material && image && material->textureTable[0].u.image == image);
    assert(varXAssetList->assetCount == 2 &&
        varXAssetList->assets[0].header.material == varXAssetList->assets[1].header.material);
    assert(g_publications.size() == 2 && g_publications[0].first == ASSET_TYPE_IMAGE &&
        g_publications[1].first == ASSET_TYPE_MATERIAL);
    WebDbImageLoadDef resource{};
    assert(DB_WebGetImageLoadDef(image, resource) && resource.byteLength == 4);
    assert(g_trace.streamOffsets[4] == 248 && g_trace.streamOffsets[7] == 0 &&
        g_trace.streamOffsets[8] == 0 && g_trace.blockAllocationCount == 2);
    std::printf("material-oracle streams=%u,%u,%u blocks=%u publications=%zu aliases=1\n",
        g_trace.streamOffsets[4], g_trace.streamOffsets[7], g_trace.streamOffsets[8],
        g_trace.blockAllocationCount, g_publications.size());
    const auto baselineMemory = PMem_GetFreeAmount();
    const auto baselineMaterialPool = DB_GetAssetPoolFreeCount(ASSET_TYPE_MATERIAL);
    const auto baselineImagePool = DB_GetAssetPoolFreeCount(ASSET_TYPE_IMAGE);
    const auto baselineResource = image->texture.webResource;
    for (unsigned attempt = 0; attempt < 8; ++attempt)
    {
        MaterialFixtureOptions failed;
        failed.corruptTrailingRawFile = true;
        failed.imagePayload = 0x12340000u + attempt;
        g_file = MakeMaterialXFile(failed);
        request = {"material-failed", 16, 0};
        DB_LoadXAssets(&request, 1, 1);
        assert(Sys_IsDatabaseReady() && DB_RuntimeGeneratedLoadFailed());
        assert(g_streamPosStackIndex == 0 && g_streamPosIndex == 0);
        assert(DB_FindXAssetHeader(ASSET_TYPE_MATERIAL, "materials/gate3").material == material);
        assert(DB_FindXAssetHeader(ASSET_TYPE_IMAGE, "images/gate3").image == image);
        assert(image->texture.webResource == baselineResource);
        assert(material->textureTable[0].u.image == image);
        assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_MATERIAL) == baselineMaterialPool);
        assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_IMAGE) == baselineImagePool);
        assert(DB_WebGetImageLoadDefStats().entryCount == 1 &&
            DB_WebGetImageLoadDefStats().encodedPayloadBytes == 4);
        assert(PMem_GetFreeAmount() == baselineMemory);
        failed.corruptTrailingRawFile = false;
        g_file = MakeMaterialXFile(failed);
        request = {"material-retry", 16, 0};
        DB_LoadXAssets(&request, 1, 1);
        assert(Sys_IsDatabaseReady() && !DB_RuntimeGeneratedLoadFailed());
        assert(image->texture.webResource != baselineResource);
        assert(DB_WebGetImageLoadDef(image, resource) && resource.byteLength == 4);
        std::uint32_t payload; std::memcpy(&payload, resource.data, sizeof(payload));
        assert(payload == failed.imagePayload);
        DB_UnloadXZonesForFreeFlags(16);
        assert(image->texture.webResource == baselineResource);
        assert(DB_WebGetImageLoadDefStats().entryCount == 1);
        assert(PMem_GetFreeAmount() == baselineMemory);
    }
    std::puts("material-oracle failures=8 retries=8 publication-identity=restored resources=restored pools=restored pmem=restored");
    DB_UnloadXZonesForFreeFlags(8);
    assert(DB_WebGetImageLoadDefStats().entryCount == 0);
    assert(PMem_GetFreeAmount() == g_arena.size());
    g_publications.clear();
    g_trace = {};
    g_file = MakeXModelXFile();
    request = {"model-oracle", 8, 0};
    DB_LoadXAssets(&request, 1, 1);
    assert(Sys_IsDatabaseReady() && !DB_RuntimeGeneratedLoadFailed());
    const auto *model = DB_FindXAssetHeader(ASSET_TYPE_XMODEL, "xmodel/gate3").model;
    assert(model && model->numBones == 1 && model->numsurfs == 1);
    assert(varXAssetList->assets[0].header.model == model &&
        varXAssetList->assets[1].header.model == model);
    assert(model->surfs[0].verts0[0].xyz[2] == 3);
    assert(model->surfs[0].vertList[0].collisionTree->leafs[0].triangleBeginIndex == 3);
    assert(model->collSurfs[0].collTris[0].plane[2] == 1);
    assert(model->physPreset && model->physPreset->mass == 5);
    assert(g_publications.size() == 2 && g_publications[0].first == ASSET_TYPE_PHYSPRESET &&
        g_publications[1].first == ASSET_TYPE_XMODEL);
    assert(g_trace.streamOffsets[4] == 640 && g_trace.streamOffsets[7] == 32 &&
        g_trace.streamOffsets[8] == 6 && g_trace.blockAllocationCount == 4);
    std::printf("xmodel-oracle streams=%u,%u,%u blocks=%u publications=%zu aliases=1 vertices=1 collision-leaves=1\n",
        g_trace.streamOffsets[4], g_trace.streamOffsets[7], g_trace.streamOffsets[8],
        g_trace.blockAllocationCount, g_publications.size());
    const auto modelMemory = PMem_GetFreeAmount();
    const auto *originalSurface = model->surfs;
    const auto *originalVertices = model->surfs[0].verts0;
    const auto modelPool = DB_GetAssetPoolFreeCount(ASSET_TYPE_XMODEL);
    const auto physicsPool = DB_GetAssetPoolFreeCount(ASSET_TYPE_PHYSPRESET);
    for (unsigned attempt = 0; attempt < 8; ++attempt)
    {
        XModelFixtureOptions failed;
        failed.corruptTrailingRawFile = true;
        failed.vertexZ = 10.0f + attempt;
        g_file = MakeXModelXFile(failed);
        request = {"model-failed", 16, 0};
        DB_LoadXAssets(&request, 1, 1);
        assert(Sys_IsDatabaseReady() && DB_RuntimeGeneratedLoadFailed());
        assert(g_streamPosStackIndex == 0 && g_streamPosIndex == 0);
        assert(DB_FindXAssetHeader(ASSET_TYPE_XMODEL, "xmodel/gate3").model == model);
        assert(model->surfs == originalSurface && model->surfs[0].verts0 == originalVertices);
        assert(originalVertices[0].xyz[2] == 3);
        assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_XMODEL) == modelPool);
        assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_PHYSPRESET) == physicsPool);
        assert(PMem_GetFreeAmount() == modelMemory);
        failed.corruptTrailingRawFile = false;
        g_file = MakeXModelXFile(failed);
        request = {"model-retry", 16, 0};
        DB_LoadXAssets(&request, 1, 1);
        assert(Sys_IsDatabaseReady() && !DB_RuntimeGeneratedLoadFailed());
        assert(model->surfs != originalSurface && model->surfs[0].verts0[0].xyz[2] == failed.vertexZ);
        assert(varXAssetList->assets[0].header.model == model &&
            varXAssetList->assets[1].header.model == model);
        DB_UnloadXZonesForFreeFlags(16);
        assert(model->surfs == originalSurface && model->surfs[0].verts0 == originalVertices);
        assert(PMem_GetFreeAmount() == modelMemory);
    }
    std::puts("xmodel-oracle failures=8 retries=8 publication-identity=restored geometry=restored pools=restored pmem=restored");
    DB_UnloadXZonesForFreeFlags(8);
    assert(PMem_GetFreeAmount() == g_arena.size());
    std::puts("independent-material-oracle image-before-material=preserved resource=retired");
    return 0;
}
