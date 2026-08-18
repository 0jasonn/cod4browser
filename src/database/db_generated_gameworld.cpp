#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_runtime_prefix.h>
#include <game/g_bsp.h>

#include <cstdint>
#include <limits>

GameWorldSp *varGameWorldSp = nullptr;
GameWorldSp **varGameWorldSpPtr = nullptr;

namespace
{
static_assert(sizeof(void *) == 4u,
    "The canonical GameWorldSp loader requires the IW3 32-bit ABI");
static_assert(sizeof(GameWorldSp) == 44u);
static_assert(sizeof(PathData) == 40u);
static_assert(sizeof(pathnode_t) == 128u);
static_assert(sizeof(pathnode_constant_t) == 68u);
static_assert(sizeof(pathlink_s) == 12u);
static_assert(sizeof(pathnode_tree_t) == 16u);

constexpr std::uint32_t MAX_PATH_TREE_DEPTH = 1024u;
constexpr std::uint32_t MAX_PATH_TREE_COUNT = PATH_MAX_NODES * 2u;

bool CheckedBytes(std::int64_t count, std::size_t stride,
    const char *stage, std::size_t &bytes)
{
    if (count < 0 || static_cast<std::uint64_t>(count) * stride >
        static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)()))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    bytes = static_cast<std::size_t>(count) * stride;
    if (!DB_RuntimeStreamCanRead(bytes))
    {
        DB_RuntimeGeneratedFailure(stage);
        return false;
    }
    return true;
}

void Load_PathLinks(pathnode_constant_t &constant)
{
    if (!constant.Links) return;
    std::size_t bytes = 0;
    if (!CheckedBytes(constant.totalLinkCount, sizeof(pathlink_s),
        "GameWorldSp/path link array", bytes)) return;
    constant.Links = reinterpret_cast<pathlink_s *>(DB_AllocStreamPos(3));
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(constant.Links),
        static_cast<std::int32_t>(bytes));
}

void Load_PathNode(pathnode_t &node)
{
    varScriptString = &node.constant.targetname;
    Load_ScriptStringArray(false, 5);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    Load_PathLinks(node.constant);
}

void Load_PathNodeArray(pathnode_t *nodes, std::uint32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedBytes(count, sizeof(pathnode_t),
        "GameWorldSp/path node array", bytes)) return;
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(nodes),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        Load_PathNode(nodes[index]);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_PathTree(pathnode_tree_t &tree, std::uint32_t depth);

void Load_PathTreePointer(pathnode_tree_t *&tree, std::uint32_t depth)
{
    if (!tree) return;
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(tree);
    if (value == UINT32_MAX)
    {
        if (depth >= MAX_PATH_TREE_DEPTH)
        {
            DB_RuntimeGeneratedFailure("GameWorldSp/path tree depth");
            return;
        }
        tree = reinterpret_cast<pathnode_tree_t *>(DB_AllocStreamPos(3));
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(tree),
            sizeof(pathnode_tree_t));
        if (!DB_RuntimeGeneratedLoadFailed()) Load_PathTree(*tree, depth + 1u);
    }
    else
    {
        DB_ConvertOffsetToPointer(reinterpret_cast<std::uint32_t *>(&tree));
    }
}

void Load_PathTree(pathnode_tree_t &tree, std::uint32_t depth)
{
    if (tree.axis < 0)
    {
        if (!tree.u.s.nodes) return;
        std::size_t bytes = 0;
        if (!CheckedBytes(tree.u.s.nodeCount, sizeof(std::uint16_t),
            "GameWorldSp/path tree node index array", bytes)) return;
        tree.u.s.nodes = reinterpret_cast<std::uint16_t *>(DB_AllocStreamPos(1));
        Load_Stream(true, reinterpret_cast<std::uint8_t *>(tree.u.s.nodes),
            static_cast<std::int32_t>(bytes));
        return;
    }
    Load_PathTreePointer(tree.u.child[0], depth);
    if (!DB_RuntimeGeneratedLoadFailed())
        Load_PathTreePointer(tree.u.child[1], depth);
}

void Load_PathTreeArray(pathnode_tree_t *trees, std::uint32_t count)
{
    std::size_t bytes = 0;
    if (!CheckedBytes(count, sizeof(pathnode_tree_t),
        "GameWorldSp/path tree array", bytes)) return;
    Load_Stream(true, reinterpret_cast<std::uint8_t *>(trees),
        static_cast<std::int32_t>(bytes));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        Load_PathTree(trees[index], 0u);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
}

void Load_PathData(PathData &path)
{
    if (path.nodeCount > PATH_MAX_NODES || path.chainNodeCount > PATH_MAX_NODES ||
        path.nodeTreeCount < 0 ||
        static_cast<std::uint32_t>(path.nodeTreeCount) > MAX_PATH_TREE_COUNT ||
        path.visBytes < 0)
    {
        DB_RuntimeGeneratedFailure("GameWorldSp/invalid path counts");
        return;
    }
    if (path.nodes)
    {
        path.nodes = reinterpret_cast<pathnode_t *>(DB_AllocStreamPos(3));
        Load_PathNodeArray(path.nodes, path.nodeCount);
        if (DB_RuntimeGeneratedLoadFailed()) return;
    }
    DB_PushStreamPos(1);
    if (path.basenodes)
    {
        std::size_t bytes = 0;
        if (CheckedBytes(path.nodeCount, sizeof(pathbasenode_t),
            "GameWorldSp/base node array", bytes))
        {
            path.basenodes = reinterpret_cast<pathbasenode_t *>(
                DB_AllocStreamPos(15));
            Load_Stream(true, reinterpret_cast<std::uint8_t *>(path.basenodes),
                static_cast<std::int32_t>(bytes));
        }
    }
    DB_PopStreamPos();
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (path.chainNodeForNode)
    {
        std::size_t bytes = 0;
        if (!CheckedBytes(path.nodeCount, sizeof(std::uint16_t),
            "GameWorldSp/chain-node array", bytes)) return;
        path.chainNodeForNode = reinterpret_cast<std::uint16_t *>(
            DB_AllocStreamPos(1));
        Load_Stream(true,
            reinterpret_cast<std::uint8_t *>(path.chainNodeForNode),
            static_cast<std::int32_t>(bytes));
    }
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (path.nodeForChainNode)
    {
        std::size_t bytes = 0;
        if (!CheckedBytes(path.nodeCount, sizeof(std::uint16_t),
            "GameWorldSp/node-for-chain array", bytes)) return;
        path.nodeForChainNode = reinterpret_cast<std::uint16_t *>(
            DB_AllocStreamPos(1));
        Load_Stream(true,
            reinterpret_cast<std::uint8_t *>(path.nodeForChainNode),
            static_cast<std::int32_t>(bytes));
    }
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (path.pathVis)
    {
        std::size_t bytes = 0;
        if (!CheckedBytes(path.visBytes, 1u,
            "GameWorldSp/path visibility", bytes)) return;
        path.pathVis = DB_AllocStreamPos(0);
        Load_Stream(true, path.pathVis, static_cast<std::int32_t>(bytes));
    }
    if (DB_RuntimeGeneratedLoadFailed()) return;
    if (path.nodeTree)
    {
        path.nodeTree = reinterpret_cast<pathnode_tree_t *>(DB_AllocStreamPos(3));
        Load_PathTreeArray(path.nodeTree,
            static_cast<std::uint32_t>(path.nodeTreeCount));
    }
}

void Load_GameWorldSp(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varGameWorldSp),
        sizeof(GameWorldSp));
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(4);
    varXString = &varGameWorldSp->name;
    Load_XString(false);
    if (!DB_RuntimeGeneratedLoadFailed()) Load_PathData(varGameWorldSp->path);
    DB_PopStreamPos();
}
} // namespace

void __cdecl Load_GameWorldSpPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart,
        reinterpret_cast<std::uint8_t *>(varGameWorldSpPtr), 4);
    if (DB_RuntimeGeneratedLoadFailed()) return;
    DB_PushStreamPos(0);
    if (*varGameWorldSpPtr)
    {
        const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(
            *varGameWorldSpPtr);
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            *varGameWorldSpPtr = reinterpret_cast<GameWorldSp *>(
                DB_AllocStreamPos(3));
            varGameWorldSp = *varGameWorldSpPtr;
            const void **inserted = value == UINT32_MAX - 1u
                ? DB_InsertPointer() : nullptr;
            Load_GameWorldSp(true);
            if (!DB_RuntimeGeneratedLoadFailed())
                Load_GameWorldSpAsset(reinterpret_cast<XAssetHeader *>(
                    varGameWorldSpPtr));
            if (!DB_RuntimeGeneratedLoadFailed())
            {
                DB_RuntimeTraceAssetLoaded((*varGameWorldSpPtr)->name);
                if (inserted) *inserted = *varGameWorldSpPtr;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(reinterpret_cast<std::uint32_t *>(
                varGameWorldSpPtr));
        }
    }
    DB_PopStreamPos();
}
