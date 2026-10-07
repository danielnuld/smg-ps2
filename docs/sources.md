# Sources ledger

Every hardware or format claim used by this project must appear here with a source.
Status: **V** verified against the source · **E** estimate (not a fact; must be replaced by a measurement) · **D** design choice (ours, not hardware) · **X** retracted (was stated without support).

Local sources are pinned by `vendor.txt` (Petari `0fac090b`, galaxy-macos `92d7ae4a`).
`refs/ps2tek.txt` = tag-stripped https://raw.githubusercontent.com/PSI-Rockin/ps2tek/master/index.html (line numbers below refer to it; regenerate: `curl -sL <url> | sed -e 's/<[^>]*>//g' > refs/ps2tek.txt`).

## Wii / Super Mario Galaxy

| Claim | Source | St |
|---|---|---|
| Broadway core clock 729 MHz, bus 243 MHz | `Petari/libs/RVL_SDK/include/revolution/os.h:23,26` | V |
| MEM2 is 64 MB (0x4000000) on retail | `Petari/src/RVL_SDK/os/OS.c:226` | V |
| MEM1 is 24 MB | not found in Petari | E |
| TEV: 16 stages, 8 texture maps, 4 indirect stages | `Petari/libs/RVL_SDK/include/revolution/gx/GXEnum.h:189-190,265-266,285-286` | V |
| Yaz0: size at +4, data at +0x10, 0-nibble = +0x12, else +2, distance+1 | `Petari/src/JSystem/JKernel/JKRDecomp.cpp:195-260` | V |
| RARC header / info block / file entry layout; folder flag = bit 1 of the 8-bit flag | `Petari/libs/JSystem/include/JSystem/JKernel/JKRArchive.hpp:17-73` | V |
| RARC dir/file/string offsets are relative to the info block (= header + mHeaderSize); file data at mHeaderSize + mFileDataOffset | `Petari/src/JSystem/JKernel/JKRCompArchive.cpp:117-123,152-153` | V |
| BTI/ResTIMG header (format @0, w @2, h @4, image count @0x18, data offset @0x1C) | `Petari/libs/JSystem/include/JSystem/JUtility/JUTTexture.hpp:7-31`; image count = 1 for a single level: `src/JSystem/JUtility/JUTTexture.cpp:29` | V |
| GX texture sizes: tile shifts per format, 32-byte tiles (64 for RGBA8), IA4 is 8 bpp | `Petari/src/RVL_SDK/gx/GXTexture.c:69-152` (`GXGetTexBufferSize`) | V |
| GX enums: `GX_VA_POS=9..TEX7=20`, comp types u8/s8/u16/s16/f32 = 0..4, comp counts | `GXEnum.h:17-46,129-140,387-395,898-900` | V |
| J3D file: block count @0x0C, blocks from 0x20; TEX1 {u16 num, texRes, names}; VTX1 {fmtList, pos, nrm, nbt, clr[2], tex[8]} | `Petari/libs/JSystem/include/JSystem/J3DGraphLoader/J3DModelLoader.hpp:49-77,187-191` | V |
| JMap/BCSV header, field item {hash, mask, u16 offset, shift, type}; type 1 inline string, 6 = offset into string table after entries | `Petari/include/Game/Util/JMapInfo.hpp:6-31`; `src/Game/Util/JMapInfo.cpp:55-70` | V |
| JMap field hash: `h = c + h*31` | `Petari/src/JSystem/JGadget/hashcode.cpp:7-13` | V |
| A galaxy loads exactly the zones in `<Galaxy>Scenario.arc:/ZoneList.bcsv` (`ZoneName`) as `/StageData/<Zone>.arc`, all at once | `Petari/src/Game/System/ScenarioDataParser.cpp:33-55,97-101`; `src/Game/Scene/StageFileLoader.cpp:14-40` | V |
| Objects load `/ObjectData/<name>.arc`, plus `Low`/`Middle` LOD archives | `Petari/src/Game/Util/ModelUtil.cpp:618`; `src/Game/LiveActor/LodCtrl.cpp:351-374` | V |
| Placement `name` = ObjectData archive name | retired: replaced by the game's preload list | X |
| Archives mounted per placement: create-table archive + `cName2ArchiveNamesTable` + `makeArchiveList` funcs; planets via table | `Petari/src/Game/NameObj/NameObjFactory.cpp` `getMountObjectArchiveList`; `src/Game/Map/PlanetMapCreator.cpp:303-365` | V |
| Scenario layer mask: common layers = 1; scenario layers = `ScenarioData.bcsv[<stage name>]` for that `ScenarioNo`, shifted left by 1 | `Petari/src/Game/System/ScenarioDataParser.cpp:217-231`; used `src/Game/Scene/StageDataHolder.cpp:99-115` | V |
| Stage archive layer folders: `jmp/{placement,mapparts,childobj,generalpos,debug,start}/{common,layera..layerp}` plus flat `jmp/list`, `jmp/path`, `camera`; names stored lower-case on disc (lookups are case-insensitive) | measured: `budget.rarc_files(paths=True)` over all `StageData/*.arc` (RMGE01) | V |
| Layer bit i = `cLayerDirName[i]` (0 Common, 1..16 LayerA..LayerP); only `Placement` and `MapParts` are filtered by the mask; `Start`, `GeneralPos`, `ChildObj` load all layers (positions/children, no own archives) | `Petari/src/Game/Scene/StageDataHolder.cpp:31-32,92-130`, `initLayerJmpInfo` | V |
| Objects are created only from the masked arrays (common `_EC` + scenario `_F4`), never from the all-layer `mPlacementObjs` | `StageDataHolder.cpp` `initPlacementInfoOrderedCommon/Scenario` | V |
| A zone is placed when its `StageObjInfo` row sits in an active layer of the stage that holds it; the zone then uses its own common + scenario layers | `StageDataHolder.cpp` `createLocalStageDataHolder`, `initAfterScenarioSelected:110-119` | V |
| Zones placed by `StageObjInfo` entries, zone id from `ZoneList` | `Petari/src/Game/Scene/StageDataHolder.cpp:662-667` | V |
| Stage archives: all `ZoneList` zones mounted async into the scene heap at scene start, none unmounted during the scene | `Petari/src/Game/Scene/StageFileLoader.cpp:14-40` | V |
| `ViewGroupCtrlCube` (area): player inside → group `ObjArg0` flagged, level `ObjArg1` 1-4; actors with JMap `ViewGroupId` then clip against the full frustum (far level 0) instead of their far-clip level. Draw/clip only; actors stay in memory | `Petari/src/Game/LiveActor/ViewGroupCtrl.cpp` (`update`), `ClippingActorInfo.cpp:18-39`, `ClippingJudge.cpp:39-41`, `JMapUtil.cpp:601-608` | V |
| `ZoneSwitch` = per-zone 128-bit switch flags; `addToZone`/`removeFromZone` = collision partitioning. Neither loads or unloads a zone | `Petari/src/Game/Map/StageSwitch.cpp:22-70`; `src/Game/Map/CollisionCategorizedKeeper.cpp` | V |
| VU1 data memory 16 KB | ps2tek:82 | V |
| `--vu-index=N`: geometry kept indexed in EE RAM, VU1 expands batches of <= N unique vertices (unique × PS2 stride + u8 index per emitted vertex + 16 B tag); N = 64 is ours, VU1 cycle cost not measured | design choice, `tools/geom.py` `shp1(pool=N)` | D |
| Wii wave data lives in the JAudio "ARAM" heap, 0xE00000 (14 MB); the 0x1E0000 audio solid heap is the JAudio system heap (banks, sequences, pools), not waves | `Petari/src/Game/AudioLib/AudParams.cpp:20-22`, `AudSystem.cpp:62-64`, `src/Game/System/Overwrite.cpp:1076-1087`, `HeapMemoryWatcher.cpp:142` | V |
| Wave bank number = first word of BAA command `ws  ` (not the WSYS header id); `loadWaveArc(bank)` loads every archive of the bank | `Petari/src/JSystem/JAudio2/JAUAudioArcInterpreter.cpp:31-36`, `JAUSectionHeap.cpp:305-356` | V |
| Banks always loaded: 0-6, 8, 11 (`loadStaticResource`), 7 (`isLoadDoneSystemInit`), 34 archive 1 for Mario; per stage SE/BGM sets and per scenario SE sets from the tables | `Petari/src/Game/AudioLib/AudSceneMgr.cpp:10-216,240-345,440-460` | V |
| JKRHeap: mStart 0x30, mEnd 0x34, mSize 0x38; JKRSolidHeap: mFreeSize 0x6C, mSolidHead 0x70, mSolidTail 0x74 | `Petari/libs/JSystem/include/JSystem/JKernel/JKRHeap.hpp:216-226`, `JKRSolidHeap.hpp:51-54` | V |
| PSX SPU ADPCM: 16-byte blocks of 28 samples | `refs/psx-spx-spu.md:151-159` (https://raw.githubusercontent.com/psx-spx/psx-spx.github.io/master/docs/ps1/spu/soundprocessingunitspu.md) | V |
| SPU2 uses the PSX SPU ADPCM format | ps2tek:111 only says the SPU2 RAM is "quadrupled from PSX's SPU" | E |
| Mesh simplifier: meshoptimizer v1.3 `meshopt_simplify` (`vendor/meshoptimizer/src/meshoptimizer.h:517`), MIT, pinned in `vendor.txt`; built as `bin/meshopt.dll` with MinGW g++ and called via ctypes (`tools/simplify.py`). Chosen over a stdlib quadric decimator for speed; the PyPI package is only a 0.2.30a0 sdist | https://github.com/zeux/meshoptimizer (tag v1.3, 9e1f07b) | D |
| GS: XYZ3/XYZF3 fills the vertex queue without a drawing kick, so a strip restart costs no extra vertex | `refs/ps2tek.txt:1946-1951` | V |
| Simplified meshes are re-stripped with meshoptimizer `optimizeVertexCacheStrip` + `stripify` (restart marker, not degenerate joins); per matrix group, never across groups | `vendor/meshoptimizer/src/meshoptimizer.h:216,627`; `tools/geom.py` `shp1(ratio=)` | D |
| VTX1 block: fmt list @+8, pos array @+0x0C (offsets from block start); fmt entry {attr, cnt, type, frac} in 16 bytes | `Petari/libs/JSystem/include/JSystem/J3DGraphLoader/J3DModelLoader.hpp:70-77`, `libs/RVL_SDK/include/revolution/gx/GXStruct.h:32-37`, `src/JSystem/J3DGraphLoader/J3DModelLoader.cpp:271-284` | V |
| JKRExpHeap used block: `u16 magic='HM'`, `u8 flags`, `u8 group`, `u32 size`, `prev`, `next` | `Petari/libs/JSystem/include/JSystem/JKernel/JKRExpHeap.hpp` (CMemBlock 0x00-0x0C); magic set `src/JSystem/JKernel/JKRExpHeap.cpp:552` | V |
| `makeArchiveList` literals counted for every branch reachable by name (args ignored) | upper bound | E |
| SHP1 block: shape count @+8, offsets @+0x0C..0x28; shape init 0x28 B; draw init {size, offset}; desc list at byte offset | `Petari/libs/JSystem/include/JSystem/J3DGraphLoader/J3DModelLoader.hpp:175-185`, `J3DShapeFactory.hpp:10-31,54-56` | V |
| Display list primitive = u8 (type\|vtxfmt) + u16 count; prim types | `Petari/src/RVL_SDK/gx/GXGeometry.c:152-153`; `GXEnum.h:701-709` | V |
| KCL prism array is 1-based (first prism at mPrismOffset + 0x10) | `Petari/src/Game/Map/KCollision.cpp:67` | V |
| Per-TU PPC section ranges | `Petari/config/RMGK01/splits.txt` | V |
| EE code ≈ PPC code (×1.007 over 84 % of ported bytes, GCC 15.2 -Os vs MWCC) | measured, `tools/codesize.py` | V (sample), E (extrapolation) |
| Wii heaps: system 0x40000, audio 0x1E0000, stationed 0x900000 + 0x1400000, file cache 0x1040400 | `HeapMemoryWatcher.cpp:139-152`; `GameSystemSceneController.cpp:113` | V |
| KCL header: pos/nrm/prism/octree offsets; prism 0x10 B | `Petari/include/Game/Map/KCollision.hpp:9-41` | V |
| BCK (ANK1): table @0x14, scale f32 @0x18, rot s16 @0x1C, trans f32 @0x20 | `Petari/libs/JSystem/include/JSystem/J3DGraphAnimator/J3DAnimation.hpp:306-317` (types from J3DAnmTransformFullData l.162-173) | V |
| Resident ("stationed") archive list, 190 entries | `Petari/src/Game/System/StationedFileInfo.cpp` | V |
| Localized files: the game tries `/<LanguagePrefix>/<path>` first and falls back to `<path>`; US prefixes `UsEnglish`/`UsSpanish`/… | `Petari/src/Game/Util/FileUtil.cpp:187-193`; `src/Game/System/Language.cpp:118` | V |
| DOL header: 7 text sizes @0x90, 11 data sizes @0xAC, bss size @0xDC | from memory, no source file; consistent with RMGE01: text+data+0x100 = 6 283 264 B = file size exactly | E |
| `LOAD_TYPE_MOUNT_RESOURCE_LUIGI` entries are loaded only when playing as Luigi, so they are excluded from the resident row | inferred from the enum name (`StationedFileInfo.hpp:13-20`); loader code not traced | E |
| `main.dol` size approximates the PS2 code + static data size | PPC→MIPS code-size ratio not measured | E |
| SMG heap reservations: system 0x40000, audio 0x1E0000, stationed MEM1 0x900000, stationed MEM2 0x1400000, 0xE00000 of MEM2 as alt-ARAM; scene heaps take the rest | `Petari/src/Game/System/HeapMemoryWatcher.cpp:119-155` | V (capacities, not usage) |
| galaxy-macos runs the whole game at 60 fps on macOS; PPC asm/paired singles have `TARGET_PC` C equivalents; SDK reimplemented on SDL3 + Aurora | `galaxy-macos/port/STATUS.md`, `port/PORTING.md` | V (as claimed by that project; not run by us) |

## PS2

| Claim | Source | St |
|---|---|---|
| EE main RAM 32 MB, **first 1 MB reserved for the kernel** | ps2tek:75 | V |
| IOP RAM 2 MB | ps2tek:84 | V |
| GS VRAM 4 MB; SPU2 RAM 2 MB | ps2tek:110-111 | V |
| EE 294.912 MHz, 16 KB I-cache, 8 KB D-cache, 16 KB scratchpad | ps2tek:345-350 | V |
| FPU single precision only; no NaN/Inf; denormals flushed to 0 | ps2tek:352,1156-1174 | V |
| `double` in C therefore runs in software on the EE | follows from the row above; exact cost not measured | E |
| VU0 4 KB/4 KB, VU1 16 KB/16 KB code/data | ps2tek:79-82,2746-2747 | V |
| VIF UNPACK V3-16 and V4-8 exist | ps2tek:2674,2717 | V |
| GS texture formats include 4-bit (14h) and 8-bit (13h) indexed; CLUT stored as PSMCT32 or PSMCT16 | ps2tek:2085-2111 | V |
| CLUT size 64 B (16×32 bit) / 1024 B (256×32 bit) | derived from the row above | V |
| GS max texture 1024×1024 | ps2tek:2101-2102 | V |
| GS destination alpha test (DATE) | ps2tek:2294-2306 | V |
| SPU2: 2 cores × 24 voices | ps2tek:290 (core 0 voices 0-23) | V |

## Retracted from earlier chat (stated without support)

| Claim | St |
|---|---|
| "EE is ~3× slower than Broadway in scalar C++" | X — only the clocks are verified; real ratio must be measured (phase 3) |
| "~50-60 % saving on meshes, 25-75 % on textures" | X — replaced by the budget tool's output |
| "Disc image ~4.4 GB, extracted ~3-4 GB, total 15-20 GB" | X — to be measured from the user's image |
| "VIF decompression is free" | X — it is done by the VIF, not the EE core (ps2tek VIF section); cost in DMA/VIF time not measured |
| "SMG shadow volumes can be emulated with DATE" | X — DATE exists (V); applicability to SMG's shadow code not studied |
| "3D All-Stars mapped the pointer to the right stick" | X — unsourced |
| "Code ~8 MB, runtime ~6 MB, asset budget 18 MB" | X — replaced by measured budget (see spec) |

## Tools

| Claim | Source | St |
|---|---|---|
| `nodtool` reads ISO, WIA/RVZ, WBFS, CISO, GCZ, NKit and extracts the data partition by default | https://raw.githubusercontent.com/encounter/nod/main/README.md (l.20-26, 47-52); `nodtool extract --help` | V |

## Design choices (ours)

| Choice | Rationale | St |
|---|---|---|
| 4 bpp GX formats (I4, C4, CMPR) → PSMT4; all others → PSMT8 | never raises bpp; CMPR→PSMT4 is lossy (per-block palettes → one 16-color palette) | D |
| Size localized files with `UsEnglish` (disc is RMGE01; Fr/Es Message.arc are +7 KB/+1.6 KB) | any one language is loaded at a time (FileUtil.cpp:187-193) | D |
| Drop mip levels above 256×256 | budget lever; GS allows up to 1024 | D |
| Vertex arrays re-encoded to 16-bit components | matches VIF UNPACK V3-16 (ps2tek:2674); precision loss not evaluated | D |
