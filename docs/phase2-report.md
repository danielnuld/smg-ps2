# Phase 2 report — full engine

## Milestone 1: resident budget

### 1.1 What the Wii keeps resident (Gateway 2 dump, `tools/stationed.py`)

The resident heaps are the two stationed heaps (6.69 MB MEM1 + 19.30 MB MEM2, full). `game_mem1/2` only hold the file
cache and the scene heaps (nested), so nothing else is resident besides WPAD (0.08 MB) and the HOME button heap (0.15).
The phase 0c dumps were not kept; the Gateway 2 dump (`tools/dolphin/dumps/hd2`, phase 1) has the same stationed
heaps (same sizes, both full).

`tools/stationed.py` walks every 'HM' block of the stationed heaps (child heaps opened, 25.99 of 25.99 MB classified):
- C++ objects by vtable. main.dol is RMGE01 and Petari's symbols are RMGK01; the two builds differ by small shifting
  offsets, so the 5 676 US vtables are aligned with the 5 594 KR `__vt__` symbols as sequences of vtable sizes
  (5 507 matched, neighbouring names consistent). Class → Petari source dir through `splits.txt`, or a prefix rule
  (J3D/JPA/JKR/JAS…).
- Archives by magic (RARC/Yaz0/U8), stacks by the 0xDEADBABE fill, video buffers by size, all-zero blocks.
- Anything else is charged to the closest preceding object in the same heap (exp heaps hand out memory in order).

| System (Wii) | MB | What it is |
|---|---|---|
| Archives | 18.15 | 231 mounted archives (MarioAnime 2.82, Effect 2.47 …) |
| Video | 3.04 | XFB ×3 (2.08), screen capture BTI RGB565 640×456 (0.58), GX FIFO (0.50) |
| Particles | 1.65 | JPA resource tree (field objects + their resource data, 1.52) and EffectSystem (0.13) |
| J3D model instances | 1.09 | materials/TEV/PE blocks, shapes, anim keys of resident models |
| Archive directories | 0.87 | JKRArchive + data (0.31), JUTNameTab + data (0.56) |
| Layouts / HUD | 0.54 | LayoutHolder, CustomTagProcessor, nw4r::lyt panes, ResFont, LayoutAnmPlayer |
| Stacks | 0.25 | 6 thread stacks |
| Zero (reserved, not written) | 0.22 | 281 blocks |
| Game system | 0.14 | GameDataPlayerStatus (0.07), FileLoaderThread (0.05), rumble, NAND, scenario parser |
| Wii-only services | 0.04 | NWC24 messenger, JUTConsole, WPadRumble |
| Unclassified | 0.02 | |
| Plus WPAD + HOME button heaps | 0.23 | |

Sound is not here: it lives in its own 1.87 MB heap, already budgeted (0.68 MB EE, phase 0c). Camera, player and
object managers are created per scene, so they are in the scene runtime phase 1 extrapolated (1.52 MB).

Archives against the asset budget: 184 of the 190 StationedFileInfo entries are in memory (the missing ones are HOME
button, config and SpeakerSe); 54 archives (0.97 MB) are outside the list by name, almost all because of how they
mount (`message.arc` has the root `usenglish`, the HOME menu is U8) plus ~40 KB of `*GalaxyScenario` data and one
131 KB Yaz0 block. So the 13.95 MB resident asset row (R = 0.99) stays the reference for the archives.

### 1.2 PS2 decision per system

Budgets are PS2 bytes in the engine's arena (`mem_report` type), checked at each later gate.

| System | Wii MB | PS2 | Budget MB | Reason |
|---|---|---|---|---|
| Archives | 18.15 | replaced (converted) | 0 | already in the resident asset row (13.95 MB) |
| Video | 3.04 | replaced (GS VRAM) | 0 | frames live in the GS; packet buffers already in phase 1's render row; this room is already in the 6.35 MB |
| Particles | 1.65 | needed | 0.40 | the converter writes particle definitions read in place; budget for the emitter table, index and per-type state |
| J3D model instances | 1.09 | replaced | 0.10 | VUMD batches read in place (phase 1); budget for the joint matrices and anim state of resident models |
| Archive directories | 0.87 | replaced | 0.02 | the blob's chunk index (read in place) |
| Layouts / HUD | 0.54 | needed | 0.25 | own 2D HUD on sprites (phase 1 font): glyph table, layout instances, text of the current screen |
| Stacks | 0.25 | needed | 0.16 | main, loader and pad/memory-card threads |
| Game system (save, loader, scenario) | 0.14 | needed | 0.10 | GameData as on the Wii (0.07) and the loader's buffers |
| Zero (reserved) | 0.22 | dropped | 0 | never written |
| Wii-only services, WPAD, HOME | 0.27 | dropped | 0 | NWC24, Wii remote, HOME menu; the pad and memory card run as IOP modules |
| Unclassified | 0.02 | — | 0.02 | kept as slack |
| **Total** | **25.99 + 0.23** | | **1.05** | |

Risk carried forward: the particle budget assumes the converted Effect.arc read in place is no bigger than the Wii
archive (counted in the asset row). Milestone 2 or later checks it when particles are converted.

### 1.3 Gate

| Row | Value | Budget |
|---|---|---|
| Resident systems (1.2) | 1.05 MB | 4.83 MB (6.35 room − 1.52 scene runtime) |
| Worst frame | no engine change in this milestone (phase 1: 5.51 ms, ×2.80 = 15.4 ms) | 16.7 ms |

**Verdict: GO**, 3.78 MB left. Most of the Wii's resident runtime is either GPU/video (3.04) or structures derived from
archives (J3D instances, particle trees, archive directories: 3.61) that a read-in-place blob does not build. The
budgets in 1.2 are targets; each later gate checks them with `mem_report`.

## Milestone 2: textures

### 2.1 Converter (`tools/tex.py`, `tools/slice.py`, blob v3)

- GX decoding of I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8 and CMPR (palette formats raise; none in Gateway 2), checked
  against a before/after sheet of the 26 textures of the slice's models.
- **PS2 rule** (same bytes as phase 0's lever): a GX mirror axis is doubled with a mirrored copy and its UVs halved
  (the GS has no mirror mode); each axis halved (box filter) down to 128; 16 colours (Pillow FASTOCTREE, alpha kept) as
  PSMT4 + a 16-entry PSMCT32 CLUT, alpha scaled to 0..0x80; no mip levels. Alpha kind from the texture: opaque,
  cut-out (only 0/255) or blended.
- Material per shape from the INF1 hierarchy; per material the first TEV stage that samples a texture gives the TEX1
  index, the coordinate source and the texture matrix (plain SRT applied per vertex, `J3DGetTextureMtx` /
  `J3DGetTextureMtxMaya`; projected types would keep raw coordinates: none in Gateway).
- VUMD v3 per vertex: position f32×4 (16 B), colour u8×4 (4 B), UV s16×2 with 12 fraction bits (4 B), each array
  padded to 16; a triangle's UVs are shifted by whole repeats towards 0. 24 B per vertex instead of 32.

| Blob (Gateway 2, R = 0.99) | Phase 1 | Milestone 2 |
|---|---|---|
| VUMD | 3.96 MB | 2.96 MB |
| TEXT (18 textures after dedup, pixels + CLUTs) | — | 141 008 B |
| Total | 5.05 MB | 4.24 MB |

UVs clamped: 0. Groups without texture: 3 (Coin, PurpleCoin, StarPiece: their only texture is generated from normals,
an environment map; they keep their flat colour). Kuribo's face animation (5 eye textures) shows the first one.

### 2.2 Engine

- Textures go to VRAM **once at load**: two 640×448 32-bit frames and Z24 leave ~736 KB, Gateway's set takes 144 KB
  (texture rows 128 pixels wide, PSMT4 needs an even buffer width). This replaces design decision 3 (per-frame upload
  over PATH3) while a scene's set fits; streaming comes back at milestone 5 if a galaxy does not fit.
- A texture change is a REF to a prebuilt A+D packet (TEX0 with CLUT load, TEX1 bilinear, CLAMP, TEST with alpha test
  for cut-outs) behind FLUSH + DIRECT in the VIF chain.
- `vu1_tex.vsm` replaces the flat microprogram: STQ (perspective-correct), colour and position per vertex; untextured
  batches use the same code with TME = 0. Colours unpack as V4-8 and UVs as V2-16 straight from the blob; batches of
  72 vertices (6n + 3 qwords in a 496-qword buffer, a multiple of 12 so the 4-byte arrays stay qword-aligned).

![Gateway 2 textured in PCSX2](renders/phase2-textured.png)

### 2.3 Gate (PCSX2, `build.sh bench`, the phase 1 path)

| Row | Value | Budget |
|---|---|---|
| Texture RAM (TEXT chunk, in the blob) | 141 008 B | part of the resident asset row (phase 0 lever: 128 px, 4 bpp) |
| VRAM | frames + Z 3.28 MB, textures + CLUTs 144 KB, ~590 KB free | 4 MB |
| Runtime (`mem_report`) | 662 160 B (render 659 376: +1 968 for texture packets and the second header table) | resident budget row "render" unchanged |
| Worst frame | 6.96 ms (logic 0.05, KCL 0.22, draw 4.63, GS finish 2.21 worst; average 6.50) | 16.7 ms |

Same end position as phase 1 (Mario −510, 290, −1982; 4 purple coins, 2 Kuribo), up to 70 720 triangles in view.
Video: `videos/2026-10-07_0923_phase2-textures_5d4b998.mp4` (PCSX2's capture slows the emulator to ~28 fps on the
host, so the bench numbers come from a second run without capture; they are emulated EE cycles either way).

**Verdict: GO** (6.96 of 16.7 ms on Gateway 2; VRAM and RAM within budget).

Flagged for milestone 5: the EE draw time went from 2.2 to 4.3-4.6 ms (1.7× more batches and two more unpacks per
batch). With phase 1's ×2.80 object rule the extrapolated OceanPhantomCave 1 frame is now 19.5 ms, over 16.7. The EE
rebuilds every model's chain each frame; prebuilt per-model chains (CALL with only the matrix changing) and larger
batches are the fix to try before the heavy-scenario gate.
