#include <universal/q_shared.h>
#if defined(KISAK_DB_SYNC_FILE_TEST) && !defined(KISAK_WEB)
#define KISAK_WEB 1
#endif
#include "database.h"
#include <database/db_semantic_trace.h>
#if defined(KISAK_WEB)
#include <database/db_generated_loaders.h>
#endif

#include <qcommon/threads.h>
#if defined(KISAK_WEB)
#include <database/db_runtime_prefix.h>
#include <web/web_database_filesystem.h>
#include <universal/physicalmemory.h>
#else
#include <win32/win_local.h>
#include <universal/com_files.h>

#include <gfx_d3d/r_image.h>
#include <gfx_d3d/r_buffers.h>
#endif

#include <limits>

//uint32_t volatile g_loadingAssets      828e3f3c     db_file_load.obj
//int32_t marker_db_file_load  828e3f40     db_file_load.obj

struct DB_LoadData // sizeof=0x68
{                                       // ...
    void* f;                            // ...
    const char* filename;               // ...
    XZoneMemory* zoneMem;               // ...
    int32_t outstandingReads;               // ...
#if defined(KISAK_WEB)
    uint32_t readOffset;
    uint32_t completedReadSize;
    bool endOfFile;
    bool inflateInitialized;
    bool failed;
    const char *failureStage;
#else
    OVERLAPPED overlapped;             // ...
#endif
    z_stream_s stream;                  // ...
    uint8_t* compressBufferStart; // ...
    uint8_t* compressBufferEnd; // ...
    void(__cdecl* interrupt)();        // ...
    int32_t allocType;                      // ...
};

#ifdef KISAK_MP
bool g_minimumFastFileLoaded;
#elif KISAK_SP
bool g_anyFastFileLoaded;
#endif

DB_LoadData g_load;
#if defined(KISAK_WEB)
int32_t g_loadedSize;
int32_t g_loadedExternalBytes;
#else
LONG g_loadedSize;
LONG g_loadedExternalBytes;
#endif
volatile int32_t g_totalSize;
volatile int32_t g_totalExternalBytes;
int32_t g_trackLoadProgress;

XAssetList g_varXAssetList;

// --- file-local forward declarations (moved out of database.h) ---
static void __cdecl DB_CancelLoadXFile();
static int32_t DB_WaitXFileStage();
static void DB_ReadXFileStage();
static int32_t __cdecl DB_ReadData();
static void Load_XAssetListCustom();
static void __cdecl Load_XAssetArrayCustom(int32_t count);

#if defined(KISAK_WEB)
static WebDatabaseFile DB_WebFile()
{
    return static_cast<WebDatabaseFile>(reinterpret_cast<std::uintptr_t>(g_load.f) - 1u);
}

static void DB_WebFail(const char *stage)
{
    if (!g_load.failed)
    {
        g_load.failed = true;
        g_load.failureStage = stage;
        DB_RuntimeTraceStage(stage);
    }
}

void DB_FailXFileLoad(const char *stage)
{
    DB_WebFail(stage);
}

bool DB_HasXFileLoadFailure()
{
    return g_load.failed;
}
#endif

void __cdecl DB_CancelLoadXFile()
{
    if (g_load.compressBufferStart)
    {
        while (g_load.outstandingReads)
            DB_WaitXFileStage();
#if defined(KISAK_WEB)
        if (g_load.inflateInitialized)
        {
            DB_AuthLoad_InflateEnd(&g_load.stream);
            g_load.inflateInitialized = false;
        }
#else
        DB_AuthLoad_InflateEnd(&g_load.stream);
#endif
        if (!g_load.f)
            MyAssertHandler(".\\database\\db_file_load.cpp", 165, 0, "%s", "g_load.f");
#if defined(KISAK_WEB)
        WebDatabaseFS_Close(DB_WebFile());
        g_load.f = nullptr;
        g_load.compressBufferStart = nullptr;
        DB_RuntimeTraceCleanupComplete();
#else
        CloseHandle(g_load.f);
#endif
    }
}

int32_t DB_WaitXFileStage()
{
    int32_t result; // eax

    if (!g_load.f)
        MyAssertHandler(".\\database\\db_file_load.cpp", 278, 0, "%s", "g_load.f");
    if (g_load.outstandingReads <= 0)
        MyAssertHandler(".\\database\\db_file_load.cpp", 280, 0, "%s", "g_load.outstandingReads > 0");
    --g_load.outstandingReads;
#if defined(KISAK_WEB)
    result = ++g_loadedSize;
    g_load.stream.avail_in += g_load.completedReadSize;
    g_load.completedReadSize = 0;
#else
    SleepEx(0xFFFFFFFF, 1);
    result = InterlockedIncrement(&g_loadedSize);
    g_load.stream.avail_in += 0x40000;
#endif
    return result;
}

void __cdecl DB_LoadedExternalData(int32_t size)
{
#if defined(KISAK_WEB)
    g_loadedExternalBytes += size;
#else
    InterlockedExchangeAdd(&g_loadedExternalBytes, size);
#endif
}

double __cdecl DB_GetLoadedFraction()
{
    double loadedBytesInternal; // [esp+14h] [ebp-20h]
    double totalBytesInternal; // [esp+1Ch] [ebp-18h]
    double loadedBytesExternal; // [esp+24h] [ebp-10h]
    double totalBytesExternal; // [esp+2Ch] [ebp-8h]

    if (!g_totalSize)
        return 0.0;
    totalBytesInternal = (double)g_totalSize * 262144.0;
    loadedBytesInternal = (double)g_loadedSize * 262144.0;
    if (loadedBytesInternal < 0.0)
        MyAssertHandler(".\\database\\db_file_load.cpp", 341, 0, "%s", "loadedBytesInternal >= 0");
    if (totalBytesInternal < loadedBytesInternal)
        loadedBytesInternal = totalBytesInternal;
    totalBytesExternal = (double)g_totalExternalBytes;
    loadedBytesExternal = (double)g_loadedExternalBytes;
    if (totalBytesExternal < loadedBytesExternal)
        loadedBytesExternal = totalBytesExternal;
    return (float)((loadedBytesInternal + loadedBytesExternal) / (totalBytesInternal + totalBytesExternal));
}

void __cdecl DB_LoadXFileData(uint8_t *pos, uint32_t size)
{
    const char *v2; // eax
    uint32_t err; // [esp+0h] [ebp-4h]

    iassert(size);
    iassert(g_load.f);
    iassert(!g_load.stream.avail_out);

    g_load.stream.next_out = pos;
    g_load.stream.avail_out = size;
    while (1)
    {
        if (!g_load.stream.avail_in)
            goto LABEL_19;
        err = DB_AuthLoad_Inflate(&g_load.stream, 2);
#if defined(KISAK_WEB)
        DB_RuntimeTraceInflate(
            static_cast<std::uint32_t>(g_load.stream.total_in),
            static_cast<std::uint32_t>(g_load.stream.total_out));
#endif
        if (err >= 2)
        {
#if defined(KISAK_WEB)
            DB_WebFail(err == Z_DATA_ERROR ? "inflate/corrupt zlib data" : "inflate/failure");
            g_load.stream.avail_out = 0;
            return;
#else
            KISAK_NULLSUB();
            DB_CancelLoadXFile();
            Com_Error(ERR_DROP, "Fastfile for zone '%s' appears corrupt or unreadable (code %i.)", g_load.filename, err + 110);
#endif
        }
        if (g_load.f)
        {
            if ((uint32_t)(g_load.stream.next_in - g_load.compressBufferStart) > 0x80000)
                MyAssertHandler(
                    ".\\database\\db_file_load.cpp",
                    392,
                    0,
                    "%s",
                    "static_cast< unsigned >( g_load.stream.next_in - g_load.compressBufferStart ) <= FILE_BUFFER_SIZE * 2");
            if (g_load.stream.next_in == g_load.compressBufferEnd)
                g_load.stream.next_in = g_load.compressBufferStart;
        }
        if (!g_load.stream.avail_out)
            break;
        if (err)
        {
#if defined(KISAK_WEB)
            DB_WebFail(err == Z_STREAM_END ? "inflate/premature end of stream" : "inflate/non-Z_OK result");
            g_load.stream.avail_out = 0;
            return;
#else
            v2 = va("Invalid fast file '%s' (%d != Z_OK)", g_load.filename, err);
            MyAssertHandler(".\\database\\db_file_load.cpp", 402, 0, "%s\n\t%s", "err == Z_OK", v2);
#endif
        }
    LABEL_19:
#if defined(KISAK_WEB)
        if (!g_load.outstandingReads)
        {
            DB_ReadXFileStage();
            if (!g_load.outstandingReads)
            {
                DB_WebFail("inflate/premature EOF");
                g_load.stream.avail_out = 0;
                return;
            }
        }
#endif
        DB_WaitXFileStage();
        DB_ReadXFileStage();
    }
}

void DB_ReadXFileStage()
{
    if (g_load.f)
    {
        if (g_load.outstandingReads)
            MyAssertHandler(".\\database\\db_file_load.cpp", 254, 0, "%s", "!g_load.outstandingReads");
#if defined(KISAK_WEB)
        if (!g_load.endOfFile && !DB_ReadData())
            DB_WebFail("XFile input/read failure");
#else
        if (!DB_ReadData() && GetLastError() != 38)
            Com_Error(ERR_DROP, "Read error of file '%s'", g_load.filename);
#endif
    }
}

int32_t __cdecl DB_ReadData()
{
    uint8_t *fileBuffer; // [esp+0h] [ebp-4h]

    if (!g_load.compressBufferStart)
        MyAssertHandler(".\\database\\db_file_load.cpp", 188, 0, "%s", "g_load.compressBufferStart");
    if (!g_load.f)
        MyAssertHandler(".\\database\\db_file_load.cpp", 189, 0, "%s", "g_load.f");
    if (g_load.interrupt)
        g_load.interrupt();
#if defined(KISAK_WEB)
    fileBuffer = &g_load.compressBufferStart[g_load.readOffset % 0x80000];
    const std::int32_t bytesRead = WebDatabaseFS_Read(
        DB_WebFile(), fileBuffer, 0x40000u);
    if (bytesRead < 0)
        return 0;
    if (bytesRead == 0)
    {
        g_load.endOfFile = true;
        return 1;
    }
    g_load.completedReadSize = static_cast<std::uint32_t>(bytesRead);
    ++g_load.outstandingReads;
    g_load.readOffset += g_load.completedReadSize;
    if (g_load.completedReadSize < 0x40000u)
        g_load.endOfFile = true;
    DB_RuntimeTraceInputRefill(g_load.completedReadSize);
    return 1;
#else
    fileBuffer = &g_load.compressBufferStart[g_load.overlapped.Offset % 0x80000];
    Sys_WaitDatabaseThread();
    if (!ReadFileEx(g_load.f, fileBuffer, 0x40000u, &g_load.overlapped, (LPOVERLAPPED_COMPLETION_ROUTINE)DB_FileReadCompletion))
        return 0;
    ++g_load.outstandingReads;
    g_load.overlapped.Offset += 0x40000;
    return 1;
#endif
}

#if !defined(KISAK_WEB)
void __stdcall DB_FileReadCompletion(
    uint32_t dwErrorCode,
    uint32_t dwNumberOfBytesTransfered,
    _OVERLAPPED *lpOverlapped)
{
    ;
}
#endif

#if !defined(KISAK_WEB)
void __cdecl DB_LoadDelayedImages()
{
    uint32_t copyIter; // [esp+0h] [ebp-4h]

    DB_EnumXAssets(ASSET_TYPE_IMAGE, (void(__cdecl *)(XAssetHeader, void *))R_DelayLoadImage, 0, 0);
    for (copyIter = 0; copyIter < g_copyInfoCount; ++copyIter)
    {
        if (g_copyInfo[copyIter]->asset.type == ASSET_TYPE_IMAGE)
            R_DelayLoadImage(g_copyInfo[copyIter]->asset.header);
    }
}

void __cdecl DB_FinishGeometryBlocks(XZoneMemory *zoneMem)
{
    if (zoneMem->lockedVertexData)
    {
        R_FinishStaticVertexBuffer((IDirect3DVertexBuffer9*)zoneMem->vertexBuffer);
        zoneMem->lockedVertexData = 0;
    }
    if (zoneMem->lockedIndexData)
    {
        R_FinishStaticIndexBuffer((IDirect3DIndexBuffer9*)zoneMem->indexBuffer);
        zoneMem->lockedIndexData = 0;
    }
}
#endif

void __cdecl DB_LoadXFileInternal()
{
    int32_t err; // [esp+8h] [ebp-4Ch]
    bool fileIsSecure; // [esp+Fh] [ebp-45h]
    uint32_t version; // [esp+10h] [ebp-44h]
    XFile file; // [esp+14h] [ebp-40h] BYREF
    int32_t fileSize; // [esp+40h] [ebp-14h]
    const char *failureReason; // [esp+44h] [ebp-10h]
    char magic[8]; // [esp+48h] [ebp-Ch] BYREF

    iassert(g_load.f);
    DB_ReadXFileStage();
    if (!g_load.outstandingReads)
#if defined(KISAK_WEB)
    {
        DB_WebFail("XFile/empty file");
        DB_CancelLoadXFile();
        DB_RuntimeTraceStop(g_load.failureStage);
        return;
    }
#else
        Com_Error(ERR_DROP, "Fastfile for zone '%s' is empty.", g_load.filename);
#endif
    DB_WaitXFileStage();
    DB_ReadXFileStage();
    if (g_load.stream.avail_in < 8)
#if defined(KISAK_WEB)
    {
        DB_WebFail("XFile/header short read");
        DB_CancelLoadXFile();
        DB_RuntimeTraceStop(g_load.failureStage);
        return;
    }
#else
        MyAssertHandler(".\\database\\db_file_load.cpp", 598, 0, "%s", "sizeof( magic ) <= g_load.stream.avail_in");
#endif
    *(uint32_t *)magic = *(uint32_t *)g_load.stream.next_in;
    *(uint32_t *)&magic[4] = *((uint32_t *)g_load.stream.next_in + 1);
    g_load.stream.next_in += 8;
    g_load.stream.avail_in -= 8;
    if (memcmp(magic, "IWff0100", 8u) && memcmp(magic, "IWffu100", 8u))
    {
#if defined(KISAK_WEB)
        DB_WebFail("XFile/invalid magic");
        DB_CancelLoadXFile();
        DB_RuntimeTraceStop(g_load.failureStage);
        return;
#else
        KISAK_NULLSUB();
        Com_Error(ERR_DROP, "Fastfile for zone '%s' is corrupt or unreadable.", g_load.filename);
#endif
    }
#if defined(KISAK_WEB)
    if (g_load.stream.avail_in < sizeof(version))
    {
        DB_WebFail("XFile/version short read");
        DB_CancelLoadXFile();
        DB_RuntimeTraceStop(g_load.failureStage);
        return;
    }
#else
    iassert(sizeof(version) <= g_load.stream.avail_in);
#endif
    version = *(uint32_t *)g_load.stream.next_in;
    g_load.stream.next_in += 4;
    g_load.stream.avail_in -= 4;
    if (version != 5)
    {
#if defined(KISAK_WEB)
        DB_WebFail(version >= 5 ? "XFile/version newer" : "XFile/version older");
        DB_CancelLoadXFile();
        DB_RuntimeTraceStop(g_load.failureStage);
        return;
#else
        if (version >= 5)
            Com_Error(
                ERR_DROP,
                "Fastfile for zone '%s' is newer than client executable (version %d, expecting %d)",
                g_load.filename,
                version,
                5);
        else
            Com_Error(
                ERR_DROP,
                "Fastfile for zone '%s' is out of date (version %d, expecting %d)",
                g_load.filename,
                version,
                5);
#endif
    }
#if defined(KISAK_WEB)
    DB_RuntimeTraceHeaderRead(12u, 0u);
    DB_RuntimeTraceStage("zone header/framing validation");
#endif
    fileIsSecure = memcmp(magic, "IWffu100", 8u) != 0;
    err = DB_AuthLoad_InflateInit(&g_load.stream, fileIsSecure);
    failureReason = 0;
    if (fileIsSecure)
        failureReason = "authenticated file not supported";
    if (err)
        failureReason = "init failed";
    if (failureReason)
    {
#if defined(KISAK_WEB)
        DB_WebFail(fileIsSecure ? "inflate/authenticated file unsupported" : "inflate/init failed");
        DB_CancelLoadXFile();
        DB_RuntimeTraceStop(g_load.failureStage);
        return;
#else
        KISAK_NULLSUB();
        DB_CancelLoadXFile();
        Com_Error(ERR_DROP, "Fastfile for zone '%s' could not be loaded (%s)", g_load.filename, failureReason);
#endif
    }
#if defined(KISAK_WEB)
    g_load.inflateInitialized = true;
    DB_RuntimeTraceInflateInitialized();
#endif
    
    DB_LoadXFileData((uint8_t *)&file, sizeof(XFile));
#if defined(KISAK_WEB)
    if (g_load.failed)
    {
        const char *stage = g_load.failureStage;
        DB_CancelLoadXFile();
        DB_RuntimeTraceStop(stage);
        return;
    }
    DB_RuntimeTraceXFile(file.size, file.externalSize, file.blockSize);

    if (!DB_CanAllocXZoneMemory(file.blockSize, g_load.allocType))
    {
        DB_WebFail("XFile/block allocation exhaustion");
        const char *stage = g_load.failureStage;
        DB_CancelLoadXFile();
        DB_RuntimeTraceStop(stage);
        return;
    }
#endif
    if (g_trackLoadProgress)
    {
#if defined(KISAK_WEB)
        const std::int64_t webFileSize = WebDatabaseFS_Size(DB_WebFile());
        fileSize = webFileSize >= 0 && webFileSize <= (std::numeric_limits<std::int32_t>::max)()
            ? static_cast<std::int32_t>(webFileSize)
            : 0;
#else
        fileSize = GetFileSize(g_load.f, 0);
#endif
        if (file.externalSize + fileSize >= 0x100000)
        {
            g_totalSize = (fileSize + 0x3FFFF) / 0x40000 - g_loadedSize;
            g_loadedSize = 0;
            g_totalExternalBytes = file.externalSize - g_loadedExternalBytes;
            g_loadedExternalBytes = 0;
        }
    }
    DB_AllocXZoneMemory(file.blockSize, g_load.filename, g_load.zoneMem, g_load.allocType);
    DB_InitStreams(g_load.zoneMem);
#if defined(KISAK_WEB)
    DB_RuntimeTraceStage("first generated-loader entry");
    Load_XAssetListCustom();
    if (!DB_RuntimeGeneratedLoadFailed())
    {
        DB_PushStreamPos(4);
        if (varXAssetList->assetCount < 0 ||
            static_cast<std::uint64_t>(varXAssetList->assetCount) * sizeof(XAsset) >
                (std::numeric_limits<std::uint32_t>::max)())
        {
            DB_RuntimeGeneratedFailure("XAssetList/excessive asset count");
        }
        else if (varXAssetList->assets)
        {
            varXAssetList->assets = AllocLoad_FxElemVisStateSample();
            varXAsset = varXAssetList->assets;
            Load_XAssetArrayCustom(varXAssetList->assetCount);
        }
        else if (varXAssetList->assetCount)
        {
            DB_RuntimeGeneratedFailure("XAssetList/missing asset array");
        }
        DB_PopStreamPos();
    }
    if (!DB_RuntimeGeneratedLoadFailed()) DB_RuntimeTraceXAssetListEnd();
    DB_CancelLoadXFile();
    DB_RuntimeTraceStop(DB_RuntimeGeneratedLoadFailed()
        ? g_load.failureStage
        : "Load_XAssetHeader/next-family-closure");
    return;
#else
    Load_XAssetListCustom();
    DB_PushStreamPos(4);
    if (varXAssetList->assets)
    {
        varXAssetList->assets = AllocLoad_FxElemVisStateSample();
        varXAsset = varXAssetList->assets;
        Load_XAssetArrayCustom(varXAssetList->assetCount);
    }
    DB_PopStreamPos();
    DB_FinishGeometryBlocks(g_load.zoneMem);
    --g_loadingAssets;
    Load_DelayStream();
    DB_LoadDelayedImages();
    iassert(g_load.compressBufferStart);
    Com_Printf(10, "Loaded zone '%s'\n", g_load.filename);
#ifdef KISAK_MP
    if (!g_minimumFastFileLoaded)
        g_minimumFastFileLoaded = I_stricmp("localized_code_post_gfx_mp", g_load.filename) == 0;
#elif KISAK_SP
	g_anyFastFileLoaded = true;
#endif
    DB_CancelLoadXFile();
#endif
}

bool __cdecl DB_IsMinimumFastFileLoaded()
{
#ifdef KISAK_MP
    return g_minimumFastFileLoaded;
#elif KISAK_SP
	return g_anyFastFileLoaded;
#endif
}

void Load_XAssetListCustom()
{
    varXAssetList = &g_varXAssetList;
    
    DB_LoadXFileData((uint8_t *)&g_varXAssetList, sizeof(XAssetList));
#if defined(KISAK_WEB)
    if (DB_HasXFileLoadFailure()) return;
    DB_RuntimeTraceXAssetListBegin(
        varXAssetList->stringList.count, varXAssetList->assetCount);
#endif
    DB_PushStreamPos(4);
    varScriptStringList = &varXAssetList->stringList;
    Load_ScriptStringList(0);
    DB_PopStreamPos();
}

void __cdecl Load_XAssetArrayCustom(int32_t count)
{
    XAsset *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    if (count < 0 || static_cast<std::uint64_t>(count) * sizeof(XAsset) >
        (std::numeric_limits<std::uint32_t>::max)())
    {
#if defined(KISAK_WEB)
        DB_RuntimeGeneratedFailure("XAssetList/excessive asset count");
        return;
#else
        iassert(count >= 0);
#endif
    }
#if defined(KISAK_WEB)
    if (!DB_RuntimeStreamCanRead(static_cast<std::size_t>(count) * sizeof(XAsset)))
    {
        DB_RuntimeGeneratedFailure("XAssetList/excessive asset count");
        return;
    }
#endif
    Load_Stream(1, (uint8_t *)varXAsset, sizeof(XAsset) * count);
#if defined(KISAK_WEB)
    if (DB_RuntimeGeneratedLoadFailed()) return;
#endif
    kisak::database::ResetNativeSemanticTraceContext();
    var = varXAsset;
    for (i = 0; i < count; ++i)
    {
        varXAsset = var;
        kisak::database::EnterNativeSemanticTraceAsset(
            static_cast<std::uint32_t>(i), varXAsset->type);
#if defined(KISAK_WEB)
        DB_SetGeneratedAssetIndex(static_cast<std::uint32_t>(i));
#endif
        Load_XAsset(0);
        kisak::database::LeaveNativeSemanticTraceAsset();
#if defined(KISAK_WEB)
        if (DB_RuntimeGeneratedLoadFailed()) return;
#endif
        ++var;
    }
}

void __cdecl DB_ResetZoneSize(int32_t trackLoadProgress)
{
    g_totalSize = 0;
    g_loadedSize = 0;
    g_totalExternalBytes = 0;
    g_loadedExternalBytes = 0;
    g_trackLoadProgress = trackLoadProgress;
}

void __cdecl DB_LoadXFile(
    const char *path,
    void *f,
    const char *filename,
    XZoneMemory *zoneMem,
    void(__cdecl *interrupt)(),
    uint8_t *buf,
    int32_t allocType)
{
#if defined(KISAK_WEB)
    (void)path;
    DB_RuntimeTraceStage("DB_LoadXFile");
#endif
    if (((uintptr_t)buf & 3) != 0)
        MyAssertHandler(".\\database\\db_file_load.cpp", 749, 0, "%s", "!(reinterpret_cast< psize_int >( buf ) & 3)");
    memset((uint8_t *)&g_load, 0, sizeof(g_load));
    g_load.f = f;
    g_load.filename = filename;
    g_load.zoneMem = zoneMem;
    g_load.interrupt = interrupt;
    g_load.allocType = allocType;
    if (g_load.compressBufferStart)
        MyAssertHandler(".\\database\\db_file_load.cpp", 762, 0, "%s", "!g_load.compressBufferStart");
    if (!g_load.f)
        MyAssertHandler(".\\database\\db_file_load.cpp", 764, 0, "%s", "g_load.f");
    if (!buf)
        MyAssertHandler(".\\database\\db_file_load.cpp", 766, 0, "%s", "buf");
    g_load.compressBufferStart = buf;
    g_load.compressBufferEnd = buf + 0x80000;
    g_load.stream.next_in = buf;
    g_load.stream.avail_in = 0;
}

