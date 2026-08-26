# Historical Gate 3 MaterialTechniqueSet generated-loader inventory

> **Historical, non-authoritative checkpoint.** This report describes the
> repository state when recorded. See the [current convergence inventory](../web-port-convergence.md).

## Source of truth

This slice follows the generated implementation in
`src/database/db_load.cpp` at `Load_MaterialTechniqueSetPtr` and its child
loaders. The Gate 2 census is not used to define any serialized layout or
pointer behavior.

The canonical dependency graph is:

```text
Load_MaterialTechniqueSetPtr
  Load_Stream(pointer cell, 4)
  DB_PushStreamPos(0)
  null                         -> no body
  -1 / -2                     -> AllocLoad_FxElemVisStateSample (align 4)
    -2                        -> DB_InsertPointer (block 4, align 4, 4 bytes)
    Load_MaterialTechniqueSet
      Load_Stream(body, 148)
      DB_PushStreamPos(4)
      Load_XString(name)
      Load_MaterialTechniquePtrArray(34)
        Load_MaterialTechniquePtr
          null                -> no technique
          -1                  -> align 4 and Load_MaterialTechnique
          prior offset        -> DB_ConvertOffsetToPointer
            Load_Stream(header, 8)
            Load_MaterialPassArray(passCount)
              Load_Stream(pass array, 20 * passCount)
              Load_MaterialPass
                vertexDecl
                  null / -1 / prior offset
                  Load_MaterialVertexDeclaration (100)
                  Load_BuildVertexDecl
                Load_MaterialVertexShaderPtr
                  null / -1 / prior offset
                  Load_MaterialVertexShader (16)
                    Load_XString(name)
                    Load_MaterialVertexShaderProgram
                      Load_GfxVertexShaderLoadDef
                        optional DWORD program array
                      Load_CreateMaterialVertexShader
                Load_MaterialPixelShaderPtr
                  null / -1 / prior offset
                  Load_MaterialPixelShader (16)
                    Load_XString(name)
                    Load_MaterialPixelShaderProgram
                      Load_GfxPixelShaderLoadDef
                        optional DWORD program array
                      Load_CreateMaterialPixelShader
                optional MaterialShaderArgument array
                  literal argument types 1/7 may contain inline float[4]
                  code-constant and hash/sampler forms remain in-cell
            Load_XString(technique name)
      DB_PopStreamPos
    Load_MaterialTechniqueSetAsset
    publish insertion cell only after successful DB publication
  prior alias                  -> DB_ConvertOffsetToAlias
  DB_PopStreamPos
```

## Canonical ABI and stream semantics

The closure is a 32-bit IW3 ABI:

| Record | Serialized size |
| --- | ---: |
| `MaterialTechniqueSet` | 148 |
| `MaterialTechnique` header | 8 |
| `MaterialPass` | 20 |
| `MaterialVertexDeclaration` | 100 |
| vertex/pixel shader | 16 |
| vertex/pixel shader program | 12 |
| vertex/pixel shader load definition | 8 |
| `MaterialShaderArgument` | 8 |
| `MaterialArgumentDef` | 4 |

The top-level pointer is read from the active asset-array stream, then the
body is allocated and read in block 0. The TechniqueSet name, the fixed array
of 34 technique pointers, all inline techniques, passes, renderer records,
shader bytecode, literal constants, and technique names are traversed while
block 4 is active. `DB_AllocStreamPos(3)` supplies every 4-byte alignment;
inline XString bytes use byte alignment.

Only the top-level asset pointer accepts `-2` and therefore owns an insertion
cell. The top-level prior token uses `DB_ConvertOffsetToAlias`. Technique,
vertex-declaration, and shader child pointers accept null, `-1`, or a prior
offset and use `DB_ConvertOffsetToPointer`; they do not accept `-2`. XStrings
accept null, `-1`, or a prior pointer offset. Shader program and pass-argument
presence fields are inline-presence indicators in the generated loader rather
than asset aliases.

Publication occurs only after the complete child closure succeeds. A `-2`
insertion cell is filled only after `Load_MaterialTechniqueSetAsset` returns a
published canonical DB header.

## Platform boundary

`Load_BuildVertexDecl`, `Load_CreateMaterialVertexShader`, and
`Load_CreateMaterialPixelShader` are renderer post-load operations, not
serialized DB behavior. Native Kisak owns their D3D implementations. The web
single-player slice compiles narrow non-rendering definitions under the same
canonical signatures; it retains the loaded canonical records and bytecode
without creating D3D objects. No browser type or asynchronous operation enters
the generated loader.

## Proven boundary

The direct x86/Wasm fixture publishes one `techsets/gate3` asset through pool
slot 0 and entry 16, fills a `-2` insertion cell at block-4 offset 16, resolves
the second asset and second technique through prior aliases, and finishes the
complete material child graph at block-4 offset 251. Pool and global free counts
change from 1,024 to 1,023 and 32,752 to 32,751 respectively. Pool or entry
exhaustion leaves the insertion cell zero and publishes no hash entry.

Normal traversal of a legally owned English `code_post_gfx.ff` publishes
TechniqueSets `sm2/2d` and `2d` as assets 0 and 1, using pool slots 0 and 1 and
entries 16 and 17. It then stops at asset 2, type 4 `ASSET_TYPE_MATERIAL`, in
block 4 at offset 16,488. The exact furthest compiled call is
`Load_XAssetHeader`; the next native generated call is `Load_MaterialHandle`.
