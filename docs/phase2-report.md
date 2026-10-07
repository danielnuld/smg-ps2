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
