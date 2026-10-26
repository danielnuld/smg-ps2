# Super Mario Galaxy on PlayStation 2

A fan port of Super Mario Galaxy (Wii, 2007) to the PlayStation 2. The game logic is ported from
[Petari](https://github.com/SMGCommunity/Petari), the community decompilation, and runs on an engine written for the
PS2 (EE + VU1 + GS, IOP sound). The game's data is converted from **your own disc** by the tools in `tools/`;
this repository has no Nintendo code or assets, and none are distributed.

Research project. Not affiliated with or endorsed by Nintendo.

## Status (October 2026)

**Playable from the title screen to the ending, on PCSX2 and on a real PS2 (SCPH-75001).**

| Area | State |
|---|---|
| Galaxies | All 136 scenarios convert and load; zones stream from the disc (packs fit 10.4 MB of EE RAM) |
| Objects | ~96 % of the placed objects have their behaviour ported (90.5 % ported, 5.2 % need none); 0.9 % missing |
| Mario | Moves, power-ups (Bee, Fire, Boo, Spring, Ice, Rainbow), swimming, launch stars, Pull Stars, ledge hang (in progress) |
| Game flow | Title, file select (six files, Mii-style icons, copy / delete), Comet Observatory with its domes, galaxy map, star list, scenario select, pause menu, star return and results, miss / game over, comets, the Butler and Rosetta's demos, Luigi (rescue, hide-and-seek, playable), the ending and staff roll |
| Movies | The THP movies re-encoded to MPEG-2, decoded by the IPU, with their subtitles |
| Saves | Memory card (`mc0:/SMGPS2`), with the PS2 browser icon |
| Sound | Sound effects per scenario (SPU2 ADPCM), streamed music, multitrack BGM states; still being cut down to fit |
| Rumble | DualShock 2 vibration where the Wii remote vibrates |
| Performance | PCSX2: 60 fps in most places. Real PS2: 26-33 fps in heavy places, 43-58 in light ones (EE bound, work in progress) |

Controls: left stick moves, X jumps, Square spins, R1 crouch, the right stick moves the star pointer, L1 shoots star
pieces, R2 grabs (Pull Stars, the dome's blue star), L2 resets the camera, D-pad left / right turns it, D-pad up looks
around, START pauses.

Next: performance on the console (the EE's draw build and logic), Good Egg fidelity against the Wii frame by frame,
sound.

## Building the disc image

You need a dump of your own **Super Mario Galaxy (USA, RMGE01)** disc. Other regions are not supported.

### Requirements

- Windows 10/11 with WSL (Ubuntu). The tools run on Windows, the PS2 build runs in WSL (paths below assume the repo
  at `D:\smg-ps2`, i.e. `/mnt/d/smg-ps2`; `engine/build.sh` finds its own root, so another place works too).
- In WSL: the [ps2dev](https://github.com/ps2dev/ps2dev) toolchain at `/usr/local/ps2dev` (EE GCC 15, ps2sdk with
  `libmpeg`, `audsrv`, `libsd`), `make`, and `genisoimage` (mkisofs).
- Python 3.12+ with `numpy` and `Pillow`.
- [ffmpeg](https://ffmpeg.org) on the PATH (movies and music).
- [nodtool](https://github.com/encounter/nod) to extract the disc.
- [meshoptimizer v1.3](https://github.com/zeux/meshoptimizer) built as `bin/meshopt.dll` (MinGW g++), used to
  simplify models:
  ```
  git clone --branch v1.3 https://github.com/zeux/meshoptimizer vendor/meshoptimizer
  g++ -O2 -shared -o bin/meshopt.dll vendor/meshoptimizer/src/*.cpp
  ```

### Steps

Run from the repository root (Git Bash; use `MSYS_NO_PATHCONV=1` before `wsl.exe` commands).

```bash
# 1. Extract the disc (the files end up in data/fs/files)
nodtool extract path/to/your/RMGE01.iso data/fs

# 2. Menus, layouts and subtitles -> out/packs/menus.bin, title.bin, subs.bin, map.bin, ...
python tools/lyt.py data/fs/files

# 3. The embedded scenario (linked into every build) and every galaxy's streaming packs (~5 min with 6 jobs)
python tools/slice.py data/fs/files HeavensDoorGalaxy 2 out/slice/hd2.bin
python tools/slice_all.py data/fs/files out/packs 6

# 4. Sound packs: the shared one, then one per scenario
python tools/sound.py --pack data/fs/files out/packs/sound.bin engine/src/se_ids.h 16000 engine/src/game.c
for s in $(find out/packs -mindepth 3 -name scene.bin | sort); do
  d=$(dirname $s)
  python tools/sound.py --pack data/fs/files $d/sound.bin engine/src/se_ids.h 16000 --scene $s engine/src/game.c
done

# 5. Music (streams, multitrack states, sequences), then into the packs
python tools/music.py data/fs/files out/music
python tools/mbgm.py data/fs/files out/music
python tools/bgmseq.py data/fs/files out/music
mkdir -p out/packs/music && cp out/music/*.pcm out/packs/music/

# 6. Movies (THP -> MPEG-2 + PCM, ~820 MB) -> out/packs/movies
python tools/movies.py data/fs/files out/packs

# 7. Memory card icon -> engine/src/save.icn, icon.sys
python tools/ps2icon.py data/fs/files

# 8. Build the release ELF (-> out/disc/SLICE.ELF) and the ISO (-> out/smg-ps2.iso)
wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh plain /mnt/d/smg-ps2/out/slice/hd2.bin EggStarGalaxy/1 disc
python tools/disc.py
```

The image (~1.5 GB) boots straight to the title screen; hold SELECT at boot for a scenario menu.

### Running it

- **Real PS2:** load `out/smg-ps2.iso` with a loader such as OPL or Neutrino (tested on an SCPH-75001 with Neutrino
  over the network). The boot file is named `SMGP_000.01`, a serial-form name launchers accept.
- **PCSX2:** recent PCSX2 refuses an image this size as a DVD. Run an ELF built from packs instead:
  `build.sh plain out/slice/hd2.bin EggStarGalaxy/1` writes `out/slice-packs.elf`, which reads `host:packs/` (start it
  with `out/` as PCSX2's host directory, e.g. `tools/run_quiet.ps1 out/slice-packs.elf`).

## Layout

| Path | What |
|---|---|
| `engine/src` | The PS2 engine and the ported game (`game.c`: actors, `menu.c`: layouts and menus, `render.c` + `vu1_tex.vsm`: GS / VU1, `audio.c`, `movie.c`, `mc.c`) |
| `engine/iop` | IOP module for the sound voices |
| `engine/test` | Host unit tests (`wsl.exe -e bash /mnt/d/smg-ps2/engine/test/run.sh`) |
| `tools` | Converters from the Wii formats (BMD/BDL models, KCL collision, BTI textures, JMap placements, BRLYT layouts, JAudio2 sound, AST music, THP movies) |
| `docs` | Design notes, reports and sources (`docs/sources.md`) |

## Credits

- [Petari](https://github.com/SMGCommunity/Petari), the Super Mario Galaxy decompilation this port follows.
- [ps2dev / ps2sdk](https://github.com/ps2dev), the PS2 toolchain and SDK.
- [meshoptimizer](https://github.com/zeux/meshoptimizer) (MIT), [nod](https://github.com/encounter/nod).
- Super Mario Galaxy is © Nintendo. This project contains none of its code or data.
