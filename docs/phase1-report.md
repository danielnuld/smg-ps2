# Phase 1 report (in progress)

## Wii reference (tasks 1.1-1.2)

HeavensDoorGalaxy scenario 2 (Gateway, purple coins), Dolphin 2609, dump 30 s after load
(`tools/dolphin/measure.ps1 HeavensDoorGalaxy 2 hd2`; checked on screen: Mysterious planet, purple-coin counter).

| Heap | Used |
|---|---|
| File cache (archives) | 3.59 MB |
| Scene heap MEM1 | 2.92 MB |
| Scene heap MEM2 (render targets) | 0.32 MB |
| **Scene heap total** | **3.24 MB**, 1 295 NameObjs |

Per class (`tools/heapclass.py`; each object charged up to the next one, so per-class bytes are approximate):

| Type | Objects | Bytes | Per object | Classes counted |
|---|---|---|---|---|
| PurpleCoin | 149 | 373.5 KB | 2 567 B | coin ×142, counter, manager, 4 circle groups, starter |
| StarPiece | 77 | 380.9 KB | 5 066 B | director pieces ×73, counter, 2 stars, director |
| Kuribo | 4 | 47.2 KB | 12 073 B | クリボー ×4 |
| Mario / player | 51 | 296.6 KB | — | hands, parts, shadows, action tables, fireballs, tornado… (matched by Petari file `Game/Player/` or name) |
| Coin | — | — | ~2 830 B | none live in this scene (only manager 10 KB, counter 6 KB); per-object value from OceanPhantomCave 1, `RailCoin` 91 objects / 257 KB |

Fixed costs that do not scale with objects: 3D particle executor 0.54 MB, pause menu 60 KB, clipping holder 40 KB,
information observer 40 KB, Yes/No layout 32 KB.

**Targets for the slice** (a sixth of the Wii, design decision 1): PurpleCoin ≤ ~430 B, StarPiece ≤ ~840 B,
Kuribo ≤ ~2 KB, Mario/player ≤ ~50 KB, scene runtime ≤ ~0.54 MB.
