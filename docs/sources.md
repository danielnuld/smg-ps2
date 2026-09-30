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
| Placement `name` = ObjectData archive name | heuristic; many actors pick their archive in code | E |
| Resident ("stationed") archive list, 190 entries | `Petari/src/Game/System/StationedFileInfo.cpp` | V |
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

## Design choices (ours)

| Choice | Rationale | St |
|---|---|---|
| 4 bpp GX formats (I4, C4, CMPR) → PSMT4; all others → PSMT8 | never raises bpp; CMPR→PSMT4 is lossy (per-block palettes → one 16-color palette) | D |
| Drop mip levels above 256×256 | budget lever; GS allows up to 1024 | D |
| Vertex arrays re-encoded to 16-bit components | matches VIF UNPACK V3-16 (ps2tek:2674); precision loss not evaluated | D |
