# Historical Gate 3 Material generated-loader inventory

## Source of truth

This slice follows `src/database/db_load.cpp` exactly from
`Load_MaterialHandle` through `Load_Material` and every generated child it can
invoke. Gate 2 census/load records are not used to define the serialized
format.

The canonical generated graph and order are:

```text
Load_MaterialHandle
  Load_Stream(pointer cell, 4)
  DB_PushStreamPos(0)
  null                         -> no body
  -1 / -2                     -> align 4, allocate Material body
    -2                        -> DB_InsertPointer (align 4, 4 bytes)
    Load_Material
      Load_Stream(body, 80)   -> block 0
      DB_PushStreamPos(4)
      Load_MaterialInfo(false)
        Load_XString(name)
      Load_MaterialTechniqueSetPtr(false)
        canonical reusable TechniqueSet family
      textureTable
        null                  -> no table
        -1                    -> align 4, MaterialTextureDef[textureCount]
        prior offset          -> DB_ConvertOffsetToPointer
          Load_MaterialTextureDef (12 each)
            semantic != 11
              Load_GfxImagePtr(false)
            semantic == 11
              null            -> no water
              -1              -> align 4, Load_water_t, Load_PicmipWater
              prior offset    -> DB_ConvertOffsetToPointer
      constantTable
        null                  -> no table
        -1                    -> align 16, MaterialConstantDef[constantCount]
        prior offset          -> DB_ConvertOffsetToPointer
      stateBitsTable
        null                  -> no table
        -1                    -> align 4, GfxStateBits[stateBitsCount]
        prior offset          -> DB_ConvertOffsetToPointer
      DB_PopStreamPos
    Load_MaterialAsset
    fill insertion cell only after successful publication
  prior alias                  -> DB_ConvertOffsetToAlias
  DB_PopStreamPos
```

The image dependency is its own reusable asset family:

```text
Load_GfxImagePtr
  Load_Stream(pointer cell, 4)
  DB_PushStreamPos(0)
  null / -1 / -2 / prior alias
  inline image
    align 4, Load_GfxImage body (36) in block 0
    DB_PushStreamPos(4)
    Load_XString(name)
    Load_GfxTextureLoad(false)
      null                    -> no pixels
      -1 / -2                -> align 4 in block 0
        -2                   -> insertion cell
        Load_GfxImageLoadDef
          header 16
          byte[resourceSize]
        Load_Texture
      prior alias             -> DB_ConvertOffsetToAlias
    DB_PopStreamPos
    Load_GfxImageAsset
    fill image insertion cell only after publication
```

`Load_water_t` reads its 68-byte body inline in block 4. A non-null `H0`
presence field becomes an aligned inline `complex_s[M * N]`; a non-null
`wTerm` becomes an aligned inline `float[M * N]`. Its final `GfxImagePtr` is
then traversed in normal generated order. These two presence fields are not
pointer-token aliases.

## ABI, blocks, and publication

| Record | Serialized size | Alignment |
| --- | ---: | ---: |
| `Material` | 80 | 4 |
| `MaterialInfo` | 24 (inside Material) | inherited |
| `MaterialTextureDef` | 12 | 4 |
| `MaterialConstantDef` | 32 | 16 |
| `GfxStateBits` | 8 | 4 |
| `water_t` | 68 | 4 |
| `complex_s` | 8 | 4 |
| `GfxImage` | 36 | 4 |
| `GfxImageLoadDef` header | 16 | 4 |

Material and image top-level bodies are block 0 allocations. Their names and
all Material tables/water children are consumed while block 4 is active.
Nested image bodies temporarily switch to block 0 and return to block 4.
Material constants alone use the generated 16-byte alignment.

Only top-level Material, top-level/nested GfxImage, and texture load-definition
pointer cells accept `-2` insertion tokens. Material table children and water
children accept null, `-1`, or prior pointer offsets exactly as shown above.
XStrings accept null, `-1`, or a prior pointer offset.

Neither Material nor a nested GfxImage is published until its complete child
graph succeeds. Consequently a Material that fails after publishing an image
may leave the completed dependency published, matching native dependency
ordering, but the Material itself and its insertion cell remain unpublished.

## Platform boundary

`Load_Texture` and `Load_PicmipWater` are renderer post-load operations. The
generated loader retains their canonical signatures and call order. The
renderer-disabled browser slice may retain canonical image/water records while
leaving GPU texture handles null and water picmip unchanged; no browser type,
promise, storage handle, or asynchronous behavior belongs in these generated
families.

## Proven boundary

The direct x86/Wasm fixture publishes nested image entry 16 before Material
entry 17, then resolves a water image through the image insertion cell. The
Material body ends at block-0 offset 136 and its complete block-4 graph ends at
offset 248. Material and image pool free counts change from 2,048 to 2,047 and
2,400 to 2,399; global free entries change from 32,752 to 32,750. Pool/entry
exhaustion and failures after image publication leave the Material insertion
cell zero.

Normal traversal of a legally owned English `code_post_gfx.ff` publishes the
nested image `3_cursor3` before Material `ui_cursor` at asset 2, followed by
top-level images `$black_3d` and `$black_cube` at assets 3 and 4. It stops at
asset 5, type 22 `ASSET_TYPE_LOCALIZE_ENTRY`, in block 4 at offset 16,568. The
exact furthest compiled call is `Load_XAssetHeader`; the next native generated
call is `Load_LocalizeEntryPtr`.
