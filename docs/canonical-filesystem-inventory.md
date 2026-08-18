# Canonical filesystem initialization inventory

This inventory records the Worker-hosted `FS_InitFilesystem` closure now used
by normal browser startup. Native Kisak remains the behavioral oracle and owns
all search-path and archive policy.

## Executed closure

```text
KisakWeb_StartCanonicalDbRuntimeCheck
  -> FS_InitFilesystem
     -> Com_StartupVariable(fs_*)
     -> SEH_InitLanguage
     -> FS_Startup("main")
        -> FS_RegisterDvars
           -> Sys_DefaultCDPath / Sys_Cwd
        -> FS_AddLocalizedGameDirectory
           -> FS_AddGameDirectory
              -> Sys_DirectoryHasContents
              -> FS_AddSearchPath
              -> FS_AddIwdFilesForGameDirectory
                 -> FS_BuildOSPath
                 -> Sys_ListFiles("iwd")
                 -> iwdsort / FS_PathCmp / IwdFileLanguage
                 -> FS_LoadZipFile
                    -> unzOpen / canonical minizip central-directory walk
                    -> FS_FileOpenReadBinary / read / seek / size / close
                    -> canonical file hash, checksum, and archive ownership
                 -> FS_AddSearchPath
        -> Com_ReadCDKey (native registry body remains disabled upstream)
        -> FS_AddCommands / FS_Path_f
     -> SEH_Init_StringEd / SEH_UpdateLanguageInfo
     -> FS_SetRestrictions
     -> FS_IsBasePathValid
        -> FS_ReadFile("fileSysCheck.cfg")
           -> FS_FOpenFileRead / FS_Read / FS_FCloseFile
```

`Sys_Cwd` returns the logical root `.` in the Worker. No OPFS path, import ID,
URL, or browser handle enters a dvar or `searchpath_t`.

## Ownership classification

| Dependency | Classification | Current owner |
| --- | --- | --- |
| `FS_InitFilesystem`, `FS_Startup`, `FS_AddGameDirectory`, `FS_AddSearchPath` | canonical portable Kisak | `universal/com_files.cpp` |
| game/localized directory order, IWD filtering/sort/precedence, duplicate lookup, pure/reference fields | canonical portable Kisak | `universal/com_files.cpp` |
| IWD central-directory walk, member inflate, archive clone/lifetime, archive seek | canonical portable Kisak | `qcommon/unzip.cpp` plus zlib |
| logical current directory | browser Sys primitive | `web_system_files.cpp` returns `.` |
| file/directory stat and direct-child enumeration | browser Worker filesystem primitive | `web_worker_filesystem.cpp` -> `worker_sync_filesystem.mjs` |
| read-only open, size, absolute seek, read, close | browser Worker filesystem primitive | `web_worker_filesystem.cpp` -> synchronous OPFS access handles |
| import, validation, persistence, and mount preparation | browser host operation | `asset_store.mjs` and Worker mount, completed before engine entry |
| CD install path, CD-key registry, native file copying, writable home data | optional native feature | empty/disabled or fail-closed for the initial offline SP slice |

The Worker `directories`/`directoryEntries` index is only generic mounted
storage metadata. It does not encode game directories, search precedence,
packs, localized policy, or file lookup policy.

## Proven semantics

The generated browser fixture persists the complete selected logical root,
including loose files not named by the minimum retail validation profile. It
contains colliding `collision.txt` members in `iw_00.iwd`, `iw_01.iwd`, and
`main/collision.txt`. Canonical C++ discovers all archives and selects the
`iw_01.iwd` member, matching native reverse insertion after ascending IWD sort.
It also proves loose-file reads, missing-file behavior, merged `FS_ListFiles`,
file size, and deflated-member absolute seek/read.

The generated archive fixture also compiles and executes the exact
`qcommon/unzip.cpp` implementation on native x86 and Wasm. Both variants prove
the same member metadata, deflated contents, end-of-file result, and
discard-read seek behavior. The broader bounded Gate 2 archive suite remains a
differential safety oracle rather than production runtime state.

The minizip discard-read path was repaired so the existing canonical
`FS_Seek` implementation can advance within a compressed member without a
destination buffer. `Dvar_SetDomainFunc` was also made ABI-correct instead of
calling a `DvarValue` callback through an x86-only flattened function type.

Host boundary limits are 8,191 mounted files, 255 UTF-8 bytes per logical path,
and 2,147,483,647 bytes per file (the canonical signed-int length ceiling).
Absolute paths, empty segments, `.`/`..`, control
characters, file/directory conflicts, stale sizes, and namespace escapes are
rejected before engine execution. Malformed IWDs remain canonical minizip
failures and do not create search paths. Browser coverage injects primitive
open, read, seek, and directory-enumeration errors and verifies that canonical
lookups fail closed; nonexistent and content-empty directory results remain
equivalent to native `Sys_ListFiles`/`Sys_DirectoryHasContents` behavior.

## Temporary filesystem code

| Component | Disposition |
| --- | --- |
| cooperative qcommon startup-file stat/read machine | retain temporarily as a browser lifecycle regression oracle; canonical runtime no longer depends on it |
| Gate 2 IWD parser/archive job | test/oracle only; canonical runtime uses Kisak minizip |
| `web_engine_filesystem.*` decoded-member cache | retain for Gate 2/renderer regression tests only |
| `web_database_filesystem.*` | retain as a thin compatibility facade over the genuine generic Worker primitive until DB callers move to canonical `FS_*` |
| `web_worker_filesystem.*` and Worker mount | genuine browser platform boundary |

Normal startup now uses canonical `FS_*` for filesystem initialization and
continues into the existing canonical DB/runtime progression. No JavaScript
search path or mounted-IWD registry exists.
