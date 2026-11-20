# Where the port stands (resume notes)

Last updated 2026-11-19. Short on purpose: the state, the rules, how to run things, what is next. The notes of every
fix and feature (cause, test mode, what was left out) are in `docs/history.md`: search it by PR number, class or test
mode, do not read it whole. New notes go at the top of its first list.

## State

- Playable on PCSX2 and on a real PS2 (SCPH-75001, the ISO read over the network from nuld: Orbit + Neutrino udpfs)
  from the title through the opening, the Gateway, the first Grand Star and the Observatory into the Terrace dome.
- Last ISO: 2026-11-17 (main c04897f: the five console reports #555-#559, a full repack of 592 packs, smoke.ps1
  passed), on nuld. Nothing released since.
- Sprint 2026-11-18 .. 22 (the whole port this week): about 170 PRs merged after that ISO, most of them classes
  and gaps ported from Petari by parallel agents and merged on a build, the host tests, memcheck and the route test
  (EggStarGalaxy/1 `star got, f 4281`), NOT each on a run in PCSX2. What still wants its run:
  `D:/smg-ps2-wt/unverified-2026-11-18.md` and `-19.md` (one line a PR: what to run, what to see). The packs in
  `out/packs` are the 2026-11-17 ones: many of those PRs changed a converter (pack formats too: VGRP #731, RGPH #745,
  lyt.py #685 / #733, the appear-SE flag #737) and wait for the full repack; their lines are in
  `D:/smg-ps2-wt/repack-todo.md`. Until then a PR that needs new packs is run on an overlay slice of its galaxy
  (`D:/smg-ps2-wt/lead-slice.sh`, `XF=-DPACKS_OVERLAY=wt-<name>`).
- Memory: the ELF's bss is the limit (the packs' region shrinks in 256 KB steps): `tools/memcheck.sh <elf>
  out/mem-baseline-plain.elf` after every plain build; per-class tables go to the scene's arena, not to statics.
- The user tests each ISO on the console and reports with a capture; the Wii reference is the walkthrough
  `C:\Users\danie\Videos\smg-wii-reference\` (iFAT6BqhE5A).

## Rules

- Fidelity first (user requirement, 2026-10-11): behaviour is ported exactly as Petari (vendor/Petari) has it -
  constants, reference frames, joints and hit sensors, nerves and their order, timings, clip names. No values
  guessed from data and no approximate versions where the decompiled code exists. Only where Petari lacks the code
  (or the PS2 lacks the service) is a gap filled, marked `ponytail:` naming it. 30 fps is acceptable.
- Our disc is RMGE01 (US): where Petari has `#if VERSION` branches, port the RMGE01 one.
- Every change: a GitHub issue, a branch `fix/<n>-...` / `feat/<n>-...`, a PR to main ("Closes #n"), its note in
  `docs/history.md`. No game data in the repo (private paths: `.git/info/exclude`).
- Pack names are 31 characters at most and `slice.name32` cuts them silently ("<model>#Open" did not fit for
  AstroDomeEntranceObservatory: #551).

## How to run

From Git Bash, WSL is called as `MSYS_NO_PATHCONV=1 wsl.exe -e bash /mnt/d/smg-ps2/...`. Converters (slice.py and all
that import it) run only in WSL with `~/smgvenv/bin/python` (Smart App Control blocks bin/meshopt.dll on Windows).

| What | Command |
|---|---|
| Build a test ELF from the packs | `wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh MODE out/slice/hd2.bin Galaxy/n` -> `out/slice-packs-MODE.elf`; extra defines: `bash -c "XF=-DNAME=1 bash .../build.sh ..."`. The modes: the header of `engine/build.sh` and `docs/history.md` ("How to run") |
| Run it in PCSX2, in the background | from `out/`: `tools/run_quiet.ps1 -Elf <elf> -Wait <s>`, picture `tools/snapbg.ps1 <png> [-N shots -Ms between]`, log `Documents/PCSX2/logs/emulog.txt` |
| Several test modes in a row | `tools/regress.ps1 "mode:Galaxy/n:seconds:pattern" ...` |
| Smoke test before an ISO | `tools/smoke.ps1` (the opening, the Gateway, the grand star's return through the Terrace's door, a Good Egg star: FAIL lines and exit status 1) |
| Host tests | `wsl.exe -e bash /mnt/d/smg-ps2/engine/test/run.sh` |
| Repack (all, or some galaxies) | `wsl.exe -e bash /mnt/d/smg-ps2/tools/release.sh [Galaxy,Galaxy...]`: on the WSL disk (`~/smg-repack`), the packs that changed copied back to `out/packs` |
| ISO, and to nuld | `build.sh plain out/slice/hd2.bin FileSelect/1 disc`, then `wsl.exe -e bash /mnt/d/smg-ps2/tools/iso.sh [nuld \| nuld-ts \| -]` (-> `~/smg-repack/out/smg-ps2.iso`, rsync to nuld, udpfs restarted when idle) |

D: is slow for writes and for many small files (drvfs): anything heavy runs on the WSL disk, as build.sh, release.sh
and iso.sh do. The console's log over the network: `tools/udptty.py` (UDP 18194).

## Release flow

1. Fixes are gathered: one ISO for a batch, not one per fix (each ISO costs the user a session on the console).
2. A fix that changes a converter says which galaxies it needs repacked (its PR and its history note); engine-only
   fixes need no repack.
3. `release.sh` (the galaxies named, or all), `smoke.ps1`, the disc ELF, `iso.sh`.
4. The user plays it; what they report becomes issues.

## Next

1. The sprint's queue, in this order: the open PRs run and merged; the two unverified lists worked down (each line
   run on an overlay slice, what is wrong fixed); then ONE full repack (`release.sh`), `smoke.ps1`, the disc ELF,
   `iso.sh nuld`, and the user's console test of that ISO. Open leads: the audit issues (#649, #650, #653, #684,
   #691), the light volumes (#790), sound (#15: `docs/sound-gaps.md` must be regenerated first, its table is older
   than the sprint).
2. The opening against the Wii (the user's capture of 2026-11-10): Mario stands inside Rosalina when she appears
   (should stand apart); the save screen after the tutorial stayed black for a while; then a scene by scene
   comparison of the whole opening (prologue, Bowser's attack, Gateway, Grand Star, the Observatory's talks).
3. The network on the console: nuld's ARP flux (eno1 + wlp1s0 on one subnet: `arp_ignore=1`, `arp_announce=2`, the user
   runs it with sudo) and the udpfs client that never reconnects (danielnuld/nuld PR #25, patched `udpfs_fhi.irx`,
   untested): the cause of "slice: bad blob" on 2026-11-09.
4. Leftovers: AstroMapObj's Open nerve (the door opens after the result, not in the demo; #551), GrandStar's demo
   nerves (#548), the glass's tint grey and no IndBendZara (#550), Mario's colour / eyelid swaps maybe baked flat
   (#540), Koopa's boot TLB miss on the console (#525), sound.bin's sample rates, the file select's water band, the
   plaza after the attack.
5. Then, as the release plan goes: every menu / UI / game flow as the original, Good Egg against the Wii frame by
   frame, sound last.
6. Repack time left on the table (#553): `vu_groups` / `strip_batches` run again for every scenario of a galaxy (a
   fifth of slice_all), tex.refine_palette (a sixth).
